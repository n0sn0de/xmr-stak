/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  * Additional permission under GNU GPL version 3 section 7
  *
  * If you modify this Program, or any covered work, by linking or combining
  * it with OpenSSL (or a modified version of that library), containing parts
  * covered by the terms of OpenSSL License and SSLeay License, the licensors
  * of this Program grant you additional permission to convey the resulting work.
  *
  */

/**
 * executor.cpp — Central event loop and pool manager
 *
 * The executor is a singleton that coordinates all mining activity:
 *
 *   1. Starts GPU backend threads (CUDA / OpenCL)
 *   2. Connects to configured pools (multi-pool with weighted failover)
 *   3. Runs a blocking event loop processing events from:
 *      - Pool connections (job notifications, socket errors)
 *      - Mining threads (share results, GPU errors)
 *      - Timed events (hashrate printing, pool re-evaluation)
 *      - User input (hashrate/results/connection status)
 *      - HTTP API requests
 *
 * Event flow: GPU threads and pool sockets push events into a thread-safe
 * queue (thdq). The executor pops events one at a time and handles them
 * sequentially — no concurrent access to pool state.
 *
 * A separate clock thread (ex_clock_thd) ticks every 500ms, managing
 * timed events like periodic hashrate reporting and reconnect delays.
 *
 * See docs/POOL-NETWORK.md for the full protocol and architecture docs.
 */

#include "executor.hpp"
#include "n0s/jconf.hpp"
#include "n0s/net/jpsock.hpp"

#include "telemetry.hpp"
#include "n0s/backend/backendConnector.hpp"
#include "n0s/backend/globalStates.hpp"
#include "n0s/backend/iBackend.hpp"
#include "n0s/backend/miner_work.hpp"

#include "n0s/http/webdesign.hpp"
#include "n0s/misc/banner.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/misc/gpu_telemetry.hpp"
#include "n0s/version.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <string>
#include <thread>
#include <ctime>



executor::executor()
{
}

void executor::push_timed_event(ex_event&& ev, size_t sec)
{
	std::unique_lock<std::mutex> lck(timed_event_mutex);
	lTimedEvents.emplace_back(std::move(ev), sec_to_ticks(sec));
}

void executor::ex_clock_thd()
{
	size_t tick = 0;
	while(true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(size_t(iTickTime)));

		push_event(ex_event(EV_PERF_TICK));

		//Eval pool choice every fourth tick
		if((tick++ & 0x03) == 0)
			push_event(ex_event(EV_EVAL_POOL_CHOICE));

		// Service timed events
		std::unique_lock<std::mutex> lck(timed_event_mutex);
		std::list<timed_event>::iterator ev = lTimedEvents.begin();
		while(ev != lTimedEvents.end())
		{
			ev->ticks_left--;
			if(ev->ticks_left == 0)
			{
				push_event(std::move(ev->event));
				ev = lTimedEvents.erase(ev);
			}
			else
				ev++;
		}
		lck.unlock();
	}
}

bool executor::get_live_pools(std::vector<jpsock*>& eval_pools)
{
	size_t limit = jconf::inst()->GetGiveUpLimit();
	size_t wait = jconf::inst()->GetNetRetry();

	if(limit == 0)
		limit = (-1); //No limit = limit of 2^64-1

	size_t pool_count = 0;
	size_t over_limit = 0;
	for(jpsock& pool : pools)
	{
		// Only eval live pools
		size_t num, dtime;
		if(pool.get_disconnects(num, dtime))
			set_timestamp();

		if(dtime == 0 || (dtime >= wait && num <= limit))
			eval_pools.emplace_back(&pool);

		pool_count++;
		if(num > limit)
			over_limit++;
	}

	if(eval_pools.size() == 0)
	{
		if(n0s::globalStates::inst().pool_id != invalid_pool_id)
		{
			printer::inst()->print_msg(L0, "All pools are dead. Idling...");
			n0s::pool_data dat;
			n0s::globalStates::inst().switch_work(n0s::miner_work(), dat);
		}

		if(over_limit == pool_count)
		{
			printer::inst()->print_msg(L0, "All pools are over give up limit. Exiting.");
			exit(0);
		}

		return false;
	}

	return true;
}

/*
 * This event is called by the timer and whenever something relevant happens.
 * The job here is to decide if we want to connect, disconnect, or switch jobs (or do nothing)
 */
void executor::eval_pool_choice()
{
	std::vector<jpsock*> eval_pools;
	eval_pools.reserve(pools.size());

	if(!get_live_pools(eval_pools))
		return;

	size_t running = 0;
	for(jpsock* pool : eval_pools)
	{
		if(pool->is_running())
			running++;
	}

	// Special case - if we are without a pool, connect to all find a live pool asap
	if(running == 0)
	{
		for(jpsock* pool : eval_pools)
		{
			if(pool->can_connect())
			{
				printer::inst()->print_msg(L1, "Fast-connecting to %s pool ...", pool->get_pool_addr());
				std::string error;
				if(!pool->connect(error))
					log_socket_error(pool, std::move(error));
			}
		}

		return;
	}

	std::sort(eval_pools.begin(), eval_pools.end(), [](jpsock* a, jpsock* b) { return b->get_pool_weight(true) < a->get_pool_weight(true); });
	jpsock* goal = eval_pools[0];

	if(goal->get_pool_id() != n0s::globalStates::inst().pool_id)
	{
		if(!goal->is_running() && goal->can_connect())
		{
			printer::inst()->print_msg(L1, "Connecting to %s pool ...", goal->get_pool_addr());

			std::string error;
			if(!goal->connect(error))
				log_socket_error(goal, std::move(error));
			return;
		}

		if(goal->is_logged_in())
		{
			pool_job oPoolJob;
			if(!goal->get_current_job(oPoolJob))
			{
				goal->disconnect();
				return;
			}

			size_t prev_pool_id = current_pool_id;
			current_pool_id = goal->get_pool_id();
			on_pool_have_job(current_pool_id, oPoolJob);

			jpsock* prev_pool = pick_pool_by_id(prev_pool_id);
			if(prev_pool == nullptr)
				reset_stats();

			last_usr_pool_id = invalid_pool_id;

			return;
		}
	}
	else
	{
		/* All is good - but check if we can do better */
		std::sort(eval_pools.begin(), eval_pools.end(), [](jpsock* a, jpsock* b) { return b->get_pool_weight(false) < a->get_pool_weight(false); });
		jpsock* goal2 = eval_pools[0];

		if(goal->get_pool_id() != goal2->get_pool_id())
		{
			if(!goal2->is_running() && goal2->can_connect())
			{
				printer::inst()->print_msg(L1, "Background-connect to %s pool ...", goal2->get_pool_addr());
				std::string error;
				if(!goal2->connect(error))
					log_socket_error(goal2, std::move(error));
				return;
			}
		}
	}

	for(jpsock& pool : pools)
	{
		if(goal->is_logged_in() && pool.is_logged_in() && pool.get_pool_id() != goal->get_pool_id())
			pool.disconnect(true);
	}
}

void executor::log_socket_error(jpsock* pool, std::string&& sError)
{
	std::string pool_name;
	pool_name.reserve(128);
	pool_name.append("[").append(pool->get_pool_addr()).append("] ");
	sError.insert(0, pool_name);

	vSocketLog.emplace_back(std::move(sError));
	printer::inst()->print_msg(L1, "SOCKET ERROR - %s", vSocketLog.back().msg.c_str());

	push_event(ex_event(EV_EVAL_POOL_CHOICE));
}

void executor::log_result_error(std::string&& sError)
{
	size_t i = 1, ln = vMineResults.size();
	for(; i < ln; i++)
	{
		if(vMineResults[i].compare(sError))
		{
			vMineResults[i].increment();
			break;
		}
	}

	if(i == ln) //Not found
		vMineResults.emplace_back(std::move(sError));
	else
		sError.clear();
}

void executor::log_result_ok(uint64_t iActualDiff)
{
	iPoolHashes += iPoolDiff;

	size_t ln = iTopDiff.size() - 1;
	if(iActualDiff > iTopDiff[ln])
	{
		iTopDiff[ln] = iActualDiff;
		std::sort(iTopDiff.rbegin(), iTopDiff.rend());
	}

	vMineResults[0].increment();
}

jpsock* executor::pick_pool_by_id(size_t pool_id)
{
	if(pool_id == invalid_pool_id)
		return nullptr;

	for(jpsock& pool : pools)
		if(pool.get_pool_id() == pool_id)
			return &pool;

	return nullptr;
}

void executor::on_sock_ready(size_t pool_id)
{
	jpsock* pool = pick_pool_by_id(pool_id);

	printer::inst()->print_msg(L1, "Pool %s connected. Logging in...", pool->get_pool_addr());

	if(!pool->cmd_login())
	{
		if(pool->have_call_error())
		{
			std::string str = "Login error: " + pool->get_call_error();
			log_socket_error(pool, std::move(str));
		}

		if(!pool->have_sock_error())
			pool->disconnect();
	}
}

void executor::on_sock_error(size_t pool_id, std::string&& sError, bool silent)
{
	jpsock* pool = pick_pool_by_id(pool_id);

	pool->disconnect();

	if(pool_id == current_pool_id)
		current_pool_id = invalid_pool_id;

	if(silent)
		return;

	log_socket_error(pool, std::move(sError));
}

void executor::on_pool_have_job(size_t pool_id, pool_job& oPoolJob)
{
	if(pool_id != current_pool_id)
		return;

	jpsock* pool = pick_pool_by_id(pool_id);

	n0s::pool_data dat;
	dat.iSavedNonce = oPoolJob.iSavedNonce;
	dat.pool_id = pool_id;

	n0s::globalStates::inst().switch_work(n0s::miner_work(oPoolJob.sJobID, oPoolJob.bWorkBlob,
												  oPoolJob.iWorkLen, oPoolJob.iTarget, pool->is_nicehash(), pool_id, oPoolJob.iBlockHeight),
		dat);

	if(dat.pool_id != pool_id)
	{
		jpsock* prev_pool;
		if((prev_pool = pick_pool_by_id(dat.pool_id)) != nullptr)
			prev_pool->save_nonce(dat.iSavedNonce);
	}

	if(iPoolDiff != pool->get_current_diff())
	{
		iPoolDiff = pool->get_current_diff();
		printer::inst()->print_msg(L2, "Difficulty changed. Now: %zu.", iPoolDiff);
	}

	if(dat.pool_id != pool_id)
	{
		jpsock* prev_pool;
		if(dat.pool_id != invalid_pool_id && (prev_pool = pick_pool_by_id(dat.pool_id)) != nullptr)
		{
			printer::inst()->print_msg(L2, "Pool switched.");
		}
		else
			printer::inst()->print_msg(L2, "Pool logged in.");
	}
	else
		printer::inst()->print_msg(L3, "New block detected.");
}

void executor::on_miner_result(size_t pool_id, job_result& oResult)
{
	jpsock* pool = pick_pool_by_id(pool_id);

	const char* backend_name = n0s::iBackend::getName(pvThreads.at(oResult.iThreadId)->backendType);
	uint64_t backend_hashcount, total_hashcount = 0;

	backend_hashcount = pvThreads.at(oResult.iThreadId)->iHashCount.load(std::memory_order_relaxed);
	for(size_t i = 0; i < pvThreads.size(); i++)
		total_hashcount += pvThreads.at(i)->iHashCount.load(std::memory_order_relaxed);

	if(!pool->is_running() || !pool->is_logged_in())
	{
		log_result_error("[NETWORK ERROR]");
		return;
	}

	size_t t_start = get_timestamp_ms();
	bool bResult = pool->cmd_submit(oResult.sJobID, oResult.iNonce, oResult.bResult,
		backend_name, backend_hashcount, total_hashcount, oResult.algorithm);
	size_t t_len = get_timestamp_ms() - t_start;

	if(t_len > 0xFFFF)
		t_len = 0xFFFF;
	iPoolCallTimes.push_back(static_cast<uint16_t>(t_len));

	std::string name(backend_name);
	std::transform(name.begin(), name.end(), name.begin(), ::toupper);

	if(bResult)
	{
		uint64_t* targets = reinterpret_cast<uint64_t*>(oResult.bResult);
		log_result_ok(t64_to_diff(targets[3]));

		if (pvThreads.at(oResult.iThreadId)->backendType == n0s::iBackend::BackendType::CPU)
		{
			printer::inst()->print_msg(L3, "CPU: Share accepted. Pool: %s", pool->get_pool_addr());
		}
		else
		{
			n0s::print_share_accepted(name.c_str(), pvThreads.at(oResult.iThreadId)->iGpuIndex, pool->get_pool_addr());
		}
	}
	else
	{
		if(!pool->have_sock_error())
		{
			if (pvThreads.at(oResult.iThreadId)->backendType == n0s::iBackend::BackendType::CPU)
			{
				printer::inst()->print_msg(L3, "CPU: Share rejected. Pool: %s", pool->get_pool_addr());
			}
			else
			{
				n0s::print_share_rejected(name.c_str(), pvThreads.at(oResult.iThreadId)->iGpuIndex, pool->get_pool_addr());
			}

			std::string error = pool->get_call_error();

			if(strncasecmp(error.c_str(), "Unauthenticated", 15) == 0)
			{
				printer::inst()->print_msg(L2, "Your miner was unable to find a share in time. Either the pool difficulty is too high, or the pool timeout is too low.");
				pool->disconnect();
			}

			log_result_error(std::move(error));
		}
		else
			log_result_error("[NETWORK ERROR]");
	}
}

#include <signal.h>

static void disable_sigpipe()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if(sigaction(SIGPIPE, &sa, 0) == -1)
		printer::inst()->print_msg(L1, "ERROR: Call to sigaction failed!");
}

void executor::ex_main()
{
	disable_sigpipe();

	assert(1000 % iTickTime == 0);

	n0s::miner_work oWork = n0s::miner_work();

	// \todo collect all backend threads
	pvThreads = n0s::BackendConnector::thread_starter(oWork);

	if(pvThreads.size() == 0)
	{
		printer::inst()->print_msg(L1, "ERROR: No miner backend enabled.");
		n0s_exit();
	}

	telem = std::make_unique<n0s::telemetry>(pvThreads.size());

	set_timestamp();
	size_t pc = jconf::inst()->GetPoolCount();
	bool already_have_cli_pool = false;
	size_t i = 0;
	for(; i < pc; i++)
	{
		jconf::pool_cfg cfg;
		jconf::inst()->GetPoolConfig(i, cfg);
#ifdef CONF_NO_TLS
		if(cfg.tls)
		{
			printer::inst()->print_msg(L1, "ERROR: No miner was compiled without TLS support.");
			n0s_exit();
		}
#endif

		if(!n0s::params::inst().poolURL.empty() && n0s::params::inst().poolURL == cfg.sPoolAddr)
		{
			auto& params = n0s::params::inst();
			already_have_cli_pool = true;

			const char* wallet = params.poolUsername.empty() ? cfg.sWalletAddr : params.poolUsername.c_str();
			const char* rigid = params.userSetRigid ? params.poolRigid.c_str() : cfg.sRigId;
			const char* pwd = params.userSetPwd ? params.poolPasswd.c_str() : cfg.sPasswd;
			bool nicehash = cfg.nicehash || params.nicehashMode;
			bool tls = params.poolUseTls;

			pools.emplace_back(i + 1, cfg.sPoolAddr, wallet, rigid, pwd, 9.9, tls, cfg.tls_fingerprint, nicehash);
		}
		else
			pools.emplace_back(i + 1, cfg.sPoolAddr, cfg.sWalletAddr, cfg.sRigId, cfg.sPasswd, cfg.weight, cfg.tls, cfg.tls_fingerprint, cfg.nicehash);
	}

	if(!n0s::params::inst().poolURL.empty() && !already_have_cli_pool)
	{
		auto& params = n0s::params::inst();
		if(params.poolUsername.empty())
		{
			printer::inst()->print_msg(L1, "ERROR: You didn't specify the username / wallet address for %s", n0s::params::inst().poolURL.c_str());
			n0s_exit();
		}

		pools.emplace_back(i + 1, params.poolURL.c_str(), params.poolUsername.c_str(), params.poolRigid.c_str(), params.poolPasswd.c_str(), 9.9, params.poolUseTls, "", params.nicehashMode);
	}

	ex_event ev;
	std::thread clock_thd(&executor::ex_clock_thd, this);

	eval_pool_choice();

	// Place the default success result at position 0, it needs to
	// be here even if our first result is a failure
	vMineResults.emplace_back();

	// If the user requested it, start the autohash printer
	if(jconf::inst()->GetVerboseLevel() >= 4)
		push_timed_event(ex_event(EV_HASHRATE_LOOP), jconf::inst()->GetAutohashTime());

	size_t cnt = 0;
	while(true)
	{
		ev = oEventQ.pop();
		switch(ev.iName)
		{
		case EV_SOCK_READY:
			on_sock_ready(ev.iPoolId);
			break;

		case EV_SOCK_ERROR:
			on_sock_error(ev.iPoolId, std::move(ev.oSocketError.sSocketError), ev.oSocketError.silent);
			break;

		case EV_POOL_HAVE_JOB:
			on_pool_have_job(ev.iPoolId, ev.oPoolJob);
			break;

		case EV_MINER_HAVE_RESULT:
			on_miner_result(ev.iPoolId, ev.oJobResult);
			break;

		case EV_EVAL_POOL_CHOICE:
			eval_pool_choice();
			break;

		case EV_GPU_RES_ERROR:
		{
			std::string err_msg = std::string(ev.oGpuError.error_str) + " GPU ID " + std::to_string(ev.oGpuError.idx);
			printer::inst()->print_msg(L0, err_msg.c_str());
			log_result_error(std::move(err_msg));
			break;
		}

		case EV_PERF_TICK:
			for(i = 0; i < pvThreads.size(); i++)
				telem->push_perf_value(i, pvThreads.at(i)->iHashCount.load(std::memory_order_relaxed),
					pvThreads.at(i)->iTimestamp.load(std::memory_order_relaxed));

			if((cnt++ & 0xF) == 0) //Every 16 ticks
			{
				double fHps = 0.0;
				double fTelem;
				bool normal = true;

				for(i = 0; i < pvThreads.size(); i++)
				{
					fTelem = telem->calc_telemetry_data(10000, i);
					if(std::isnormal(fTelem))
					{
						fHps += fTelem;
					}
					else
					{
						normal = false;
						break;
					}
				}

				if(normal && fHighestHps < fHps)
					fHighestHps = fHps;
			}

			// Record hashrate history every 2 ticks (1 second)
			if((cnt & 0x1) == 0 && !pvThreads.empty())
			{
				double per_gpu[n0s::HashrateHistory::MAX_GPUS] = {};
				double total = 0.0;
				for(i = 0; i < pvThreads.size() && i < n0s::HashrateHistory::MAX_GPUS; i++)
				{
					double h = telem->calc_telemetry_data(10000, i);
					if(std::isnormal(h))
					{
						per_gpu[i] = h;
						total += h;
					}
				}
				auto now_ms = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch())
						.count());
				hashrateHistory.push(now_ms, total, per_gpu, pvThreads.size());
			}
			break;

		case EV_USR_HASHRATE:
		case EV_USR_RESULTS:
		case EV_USR_CONNSTAT:
			print_report(ev.iName);
			break;

		case EV_HTML_HASHRATE:
		case EV_HTML_RESULTS:
		case EV_HTML_CONNSTAT:
		case EV_HTML_JSON:
		case EV_API_STATUS:
		case EV_API_HASHRATE:
		case EV_API_HASHRATE_HISTORY:
		case EV_API_GPUS:
		case EV_API_POOL:
		case EV_API_VERSION:
			http_report(ev.iName);
			break;

		case EV_HASHRATE_LOOP:
			print_report(EV_USR_HASHRATE);
			push_timed_event(ex_event(EV_HASHRATE_LOOP), jconf::inst()->GetAutohashTime());
			break;

		case EV_INVALID_VAL:
		default:
			assert(false);
			break;
		}
	}
}

inline const char* hps_format(double h, char* buf, size_t l)
{
	if(std::isnormal(h) || h == 0.0)
	{
		snprintf(buf, l, " %6.1f", h);
		return buf;
	}
	else
		return "   (na)";
}

bool executor::motd_filter_console(std::string& motd)
{
	if(motd.size() > motd_max_length)
		return false;

	motd.erase(std::remove_if(motd.begin(), motd.end(), [](int chr) -> bool { return !((chr >= 0x20 && chr <= 0x7e) || chr == '\n'); }), motd.end());
	return motd.size() > 0;
}

bool executor::motd_filter_web(std::string& motd)
{
	if(!motd_filter_console(motd))
		return false;

	std::string tmp;
	tmp.reserve(motd.size() + 128);

	for(size_t i = 0; i < motd.size(); i++)
	{
		char c = motd[i];
		switch(c)
		{
		case '&':
			tmp.append("&amp;");
			break;
		case '"':
			tmp.append("&quot;");
			break;
		case '\'':
			tmp.append("&#039");
			break;
		case '<':
			tmp.append("&lt;");
			break;
		case '>':
			tmp.append("&gt;");
			break;
		case '\n':
			tmp.append("<br>");
			break;
		default:
			tmp.append(1, c);
			break;
		}
	}

	motd = std::move(tmp);
	return true;
}

void executor::hashrate_report(std::string& out)
{
	out.reserve(2048 + pvThreads.size() * 64);

	if(jconf::inst()->PrintMotd())
	{
		std::string motd;
		for(jpsock& pool : pools)
		{
			motd.clear();
			if(pool.get_pool_motd(motd) && motd_filter_console(motd))
			{
				out.append("Message from ").append(pool.get_pool_addr()).append(":\n");
				out.append(motd).append("\n");
				out.append("-----------------------------------------------------\n");
			}
		}
	}

	char num[32];
	double fTotal[3] = {0.0, 0.0, 0.0};

	for(uint32_t b = 0; b < 4u; ++b)
	{
		// Collect raw pointers for backends matching this type
		std::vector<n0s::iBackend*> backEnds;
		for(const auto& backend_ptr : pvThreads)
		{
			if(backend_ptr->backendType == b)
				backEnds.push_back(backend_ptr.get());
		}

		size_t nthd = backEnds.size();
		if(nthd != 0)
		{
			size_t i;
			auto bType = static_cast<n0s::iBackend::BackendType>(b);
			std::string name(n0s::iBackend::getName(bType));
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);

			double fTotalCur[3] = {0.0, 0.0, 0.0};

			// Collect hashrates
			for(i = 0; i < nthd; i++)
			{
				double fHps[3];
				uint32_t tid = backEnds[i]->iThreadNo;
				fHps[0] = telem->calc_telemetry_data(10000, tid);
				fHps[1] = telem->calc_telemetry_data(60000, tid);
				fHps[2] = telem->calc_telemetry_data(900000, tid);

				fTotal[0] += (std::isnormal(fHps[0])) ? fHps[0] : 0.0;
				fTotal[1] += (std::isnormal(fHps[1])) ? fHps[1] : 0.0;
				fTotal[2] += (std::isnormal(fHps[2])) ? fHps[2] : 0.0;
				fTotalCur[0] += (std::isnormal(fHps[0])) ? fHps[0] : 0.0;
				fTotalCur[1] += (std::isnormal(fHps[1])) ? fHps[1] : 0.0;
				fTotalCur[2] += (std::isnormal(fHps[2])) ? fHps[2] : 0.0;
			}

			// Per-GPU line: hashrate + telemetry (compact, one line per GPU)
			for(i = 0; i < nthd; i++)
			{
				uint32_t gpu_idx = backEnds[i]->iGpuIndex;
				uint32_t tid = backEnds[i]->iThreadNo;
				double fHps[3];
				fHps[0] = telem->calc_telemetry_data(10000, tid);
				fHps[1] = telem->calc_telemetry_data(60000, tid);
				fHps[2] = telem->calc_telemetry_data(900000, tid);

				// Query GPU telemetry
				n0s::GpuTelemetry gt;
				bool hasTelem = false;
				if(bType == n0s::iBackend::BackendType::AMD)
					hasTelem = n0s::queryAmdTelemetry(gpu_idx, gt);
				else if(bType == n0s::iBackend::BackendType::NVIDIA)
					hasTelem = n0s::queryNvidiaTelemetry(gpu_idx, gt);

				// Format: "   GPU0 [AMD]  4531.0 H/s  54°C  17W  266.5 H/W  FAN:0%  CC:2222MHz"
				out.append("\x1B[97m   GPU").append(std::to_string(gpu_idx));
				out.append(" \x1B[2;37m[\x1B[0m\x1B[96m").append(name).append("\x1B[2;37m]\x1B[0m  ");
				out.append(n0s::format_hashrate_colored(fHps[0]));
				out.append("\x1B[2;37m H/s\x1B[0m");

				// 60s and 15m in dim if available
				if(std::isnormal(fHps[1]))
				{
					snprintf(num, sizeof(num), "  \x1B[2;37m60s:\x1B[0m\x1B[97m%.0f\x1B[0m", fHps[1]);
					out.append(num);
				}
				if(std::isnormal(fHps[2]))
				{
					snprintf(num, sizeof(num), "  \x1B[2;37m15m:\x1B[0m\x1B[97m%.0f\x1B[0m", fHps[2]);
					out.append(num);
				}

				// Telemetry inline
				if(hasTelem)
				{
					if(gt.temp_c > 0)
					{
						const char* tc = (gt.temp_c >= 85) ? "\x1B[91m" : (gt.temp_c >= 70) ? "\x1B[93m" : "\x1B[96m";
						snprintf(num, sizeof(num), "  %s%d°C\x1B[0m", tc, gt.temp_c);
						out.append(num);
					}
					if(gt.power_w > 0)
					{
						snprintf(num, sizeof(num), "  \x1B[93m%dW\x1B[0m", gt.power_w);
						out.append(num);
						if(fHps[0] > 0)
						{
							snprintf(num, sizeof(num), "  \x1B[2;37m%.1f H/W\x1B[0m", fHps[0] / gt.power_w);
							out.append(num);
						}
					}
					if(gt.fan_pct >= 0)
					{
						const char* fc = (gt.fan_pct > 80) ? "\x1B[91m" : "\x1B[2;37m";
						snprintf(num, sizeof(num), "  %sFAN:%d%%\x1B[0m", fc, gt.fan_pct);
						out.append(num);
					}
					if(gt.gpu_clock_mhz > 0)
					{
						snprintf(num, sizeof(num), "  \x1B[2;37mCC:%dMHz\x1B[0m", gt.gpu_clock_mhz);
						out.append(num);
					}
					if(gt.mem_clock_mhz > 0)
					{
						snprintf(num, sizeof(num), "  \x1B[2;37mMC:%dMHz\x1B[0m", gt.mem_clock_mhz);
						out.append(num);
					}
				}
				out.append("\n");
			}
		}
	}

	out.append("\n\x1B[1;97m    TOTAL:  \x1B[0m");
	out.append(n0s::format_hashrate_colored(fTotal[0]));
	out.append(hps_format(fTotal[1], num, sizeof(num)));
	out.append(hps_format(fTotal[2], num, sizeof(num)));
	out.append("\x1B[97m H/s\x1B[0m\n");
	out.append("\x1B[2;37m   Peak:   \x1B[0m");
	out.append(hps_format(fHighestHps, num, sizeof(num)));
	out.append("\x1B[2;37m H/s\x1B[0m\n");
	out.append("\x1B[38;5;25m   ═════════════════════════════════════════════════════════════════\x1B[0m\n");
}

char* time_format(char* buf, size_t len, std::chrono::system_clock::time_point time)
{
	time_t ctime = std::chrono::system_clock::to_time_t(time);
	tm stime;

	/*
	 * Oh for god's sake... this feels like we are back to the 90's...
	 * and don't get me started on lack strcpy_s because NIH - use non-standard strlcpy...
	 * And of course C++ implements unsafe version because... reasons
	 */

	localtime_r(&ctime, &stime);
	strftime(buf, len, "%F %T", &stime);

	return buf;
}

void executor::result_report(std::string& out)
{
	char num[128];
	char date[32];

	out.reserve(1024);

	size_t iGoodRes = vMineResults[0].count, iTotalRes = iGoodRes;
	size_t ln = vMineResults.size();

	for(size_t i = 1; i < ln; i++)
		iTotalRes += vMineResults[i].count;

	out.append("\n\x1B[96;1m    ═══ RESULT REPORT ═══\x1B[0m\n");
	out.append("\x1B[97m    Currency         : \x1B[96m").append(jconf::inst()->GetMiningCoin()).append("\x1B[0m\n");
	if(iTotalRes == 0)
	{
		out.append("\x1B[2;37m    You haven't found any results yet.\x1B[0m\n");
		return;
	}

	double dConnSec;
	{
		using namespace std::chrono;
		dConnSec = (double)duration_cast<seconds>(system_clock::now() - tPoolConnTime).count();
	}

	snprintf(num, sizeof(num), " \x1B[2;37m(%.1f %%)\x1B[0m\n", 100.0 * iGoodRes / iTotalRes);

	out.append("\x1B[97m    Difficulty       : \x1B[93m").append(std::to_string(iPoolDiff)).append("\x1B[0m\n");
	out.append("\x1B[97m    Good results     : \x1B[92m").append(std::to_string(iGoodRes)).append(" \x1B[2;37m/ ").append(std::to_string(iTotalRes)).append(num);

	if(iPoolCallTimes.size() != 0)
	{
		// Here we use iPoolCallTimes since it also gets reset when we disconnect
		snprintf(num, sizeof(num), "\x1B[96m%.1f sec\x1B[0m\n", dConnSec / iPoolCallTimes.size());
		out.append("\x1B[97m    Avg result time  : \x1B[0m").append(num);
	}
	out.append("\x1B[97m    Pool-side hashes : \x1B[96m").append(std::to_string(iPoolHashes)).append("\x1B[0m\n\n");
	out.append("\x1B[97;1m    Top 10 best results found:\x1B[0m\n");
	out.append("\x1B[2;37m    | # |            Diff | # |            Diff |\x1B[0m\n");

	for(size_t i = 0; i < 10; i += 2)
	{
		snprintf(num, sizeof(num), "\x1B[2;37m    | \x1B[96m%2zu \x1B[2;37m| \x1B[93m%16zu \x1B[2;37m| \x1B[96m%2zu \x1B[2;37m| \x1B[93m%16zu \x1B[2;37m|\x1B[0m\n",
			i, iTopDiff[i], i + 1, iTopDiff[i + 1]);
		out.append(num);
	}

	out.append("\n\x1B[97;1m    Error details:\x1B[0m\n");
	if(ln > 1)
	{
		out.append("\x1B[2;37m    | Count | Error text                       | Last seen           |\x1B[0m\n");
		for(size_t i = 1; i < ln; i++)
		{
			snprintf(num, sizeof(num), "\x1B[2;37m    | \x1B[93m%5zu \x1B[2;37m| \x1B[91m%-32.32s \x1B[2;37m| \x1B[96m%s \x1B[2;37m|\x1B[0m\n", vMineResults[i].count,
				vMineResults[i].msg.c_str(), time_format(date, sizeof(date), vMineResults[i].time));
			out.append(num);
		}
	}
	else
		out.append("\x1B[92m    Yay! No errors.\x1B[0m\n");
}

void executor::connection_report(std::string& out)
{
	char num[128];
	char date[32];

	out.reserve(512);

	jpsock* pool = pick_pool_by_id(current_pool_id);

	out.append("CONNECTION REPORT\n");
	out.append("Rig ID          : ").append(pool != nullptr ? pool->get_rigid() : "").append(1, '\n');
	out.append("Pool address    : ").append(pool != nullptr ? pool->get_pool_addr() : "<not connected>").append(1, '\n');
	if(pool != nullptr && pool->is_running() && pool->is_logged_in())
		out.append("Connected since : ").append(time_format(date, sizeof(date), tPoolConnTime)).append(1, '\n');
	else
		out.append("Connected since : <not connected>\n");

	size_t n_calls = iPoolCallTimes.size();
	if(n_calls > 1)
	{
		//Not-really-but-good-enough median
		std::nth_element(iPoolCallTimes.begin(), iPoolCallTimes.begin() + n_calls / 2, iPoolCallTimes.end());
		out.append("Pool ping time  : ").append(std::to_string(iPoolCallTimes[n_calls / 2])).append(" ms\n");
	}
	else
		out.append("Pool ping time  : (n/a)\n");

	out.append("\nNetwork error log:\n");
	size_t ln = vSocketLog.size();
	if(ln > 0)
	{
		out.append("| Date                | Error text                                             |\n");
		for(size_t i = 0; i < ln; i++)
		{
			snprintf(num, sizeof(num), "| %s | %-54.54s |\n",
				time_format(date, sizeof(date), vSocketLog[i].time), vSocketLog[i].msg.c_str());
			out.append(num);
		}
	}
	else
		out.append("Yay! No errors.\n");
}

void executor::print_report(ex_event_name ev)
{
	std::string out;
	switch(ev)
	{
	case EV_USR_HASHRATE:
		hashrate_report(out);
		break;

	case EV_USR_RESULTS:
		result_report(out);
		break;

	case EV_USR_CONNSTAT:
		connection_report(out);
		break;
	default:
		assert(false);
		break;
	}

	printer::inst()->print_str(out.c_str());
}

void executor::http_hashrate_report(std::string& out)
{
	char num_a[32], num_b[32], num_c[32], num_d[32];
	char buffer[4096];
	size_t nthd = pvThreads.size();

	out.reserve(4096);

	snprintf(buffer, sizeof(buffer), sHtmlCommonHeader, "Hashrate Report", ver_html, "Hashrate Report");
	out.append(buffer);

	bool have_motd = false;
	if(jconf::inst()->PrintMotd())
	{
		std::string motd;
		for(jpsock& pool : pools)
		{
			motd.clear();
			if(pool.get_pool_motd(motd) && motd_filter_web(motd))
			{
				if(!have_motd)
				{
					out.append(sHtmlMotdBoxStart);
					have_motd = true;
				}

				snprintf(buffer, sizeof(buffer), sHtmlMotdEntry, pool.get_pool_addr(), motd.c_str());
				out.append(buffer);
			}
		}
	}

	if(have_motd)
		out.append(sHtmlMotdBoxEnd);

	snprintf(buffer, sizeof(buffer), sHtmlHashrateBodyHigh, (unsigned int)nthd + 3);
	out.append(buffer);

	double fTotal[3] = {0.0, 0.0, 0.0};
	auto bTypePrev = static_cast<n0s::iBackend::BackendType>(0);
	std::string name;
	size_t j = 0;
	for(size_t i = 0; i < nthd; i++)
	{
		double fHps[3];
		char csThreadTag[25];
		auto bType = static_cast<n0s::iBackend::BackendType>(pvThreads.at(i)->backendType);
		if(bTypePrev == bType)
			j++;
		else
		{
			j = 0;
			bTypePrev = bType;
			name = n0s::iBackend::getName(bType);
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);
		}
		snprintf(csThreadTag, sizeof(csThreadTag),
			(99 < nthd) ? "[%s.%03u]:%03u" : ((9 < nthd) ? "[%s.%02u]:%02u" : "[%s.%u]:%u"),
			name.c_str(), (unsigned int)(j), (unsigned int)i);

		fHps[0] = telem->calc_telemetry_data(10000, i);
		fHps[1] = telem->calc_telemetry_data(60000, i);
		fHps[2] = telem->calc_telemetry_data(900000, i);

		num_a[0] = num_b[0] = num_c[0] = '\0';
		hps_format(fHps[0], num_a, sizeof(num_a));
		hps_format(fHps[1], num_b, sizeof(num_b));
		hps_format(fHps[2], num_c, sizeof(num_c));

		fTotal[0] += fHps[0];
		fTotal[1] += fHps[1];
		fTotal[2] += fHps[2];

		snprintf(buffer, sizeof(buffer), sHtmlHashrateTableRow, csThreadTag, num_a, num_b, num_c);
		out.append(buffer);
	}

	num_a[0] = num_b[0] = num_c[0] = num_d[0] = '\0';
	hps_format(fTotal[0], num_a, sizeof(num_a));
	hps_format(fTotal[1], num_b, sizeof(num_b));
	hps_format(fTotal[2], num_c, sizeof(num_c));
	hps_format(fHighestHps, num_d, sizeof(num_d));

	snprintf(buffer, sizeof(buffer), sHtmlHashrateBodyLow, num_a, num_b, num_c, num_d);
	out.append(buffer);
}

void executor::http_result_report(std::string& out)
{
	char date[128];
	char buffer[4096];

	out.reserve(4096);

	snprintf(buffer, sizeof(buffer), sHtmlCommonHeader, "Result Report", ver_html, "Result Report");
	out.append(buffer);

	size_t iGoodRes = vMineResults[0].count, iTotalRes = iGoodRes;
	size_t ln = vMineResults.size();

	for(size_t i = 1; i < ln; i++)
		iTotalRes += vMineResults[i].count;

	double fGoodResPrc = 0.0;
	if(iTotalRes > 0)
		fGoodResPrc = 100.0 * iGoodRes / iTotalRes;

	double fAvgResTime = 0.0;
	if(iPoolCallTimes.size() > 0)
	{
		using namespace std::chrono;
		fAvgResTime = ((double)duration_cast<seconds>(system_clock::now() - tPoolConnTime).count()) / iPoolCallTimes.size();
	}

	snprintf(buffer, sizeof(buffer), sHtmlResultBodyHigh,
		jconf::inst()->GetMiningCoin().c_str(),
		iPoolDiff, iGoodRes, iTotalRes, fGoodResPrc, fAvgResTime, iPoolHashes,
		iTopDiff[0], iTopDiff[1], iTopDiff[2], iTopDiff[3],
		iTopDiff[4], iTopDiff[5], iTopDiff[6], iTopDiff[7],
		iTopDiff[8], iTopDiff[9]);

	out.append(buffer);

	for(size_t i = 1; i < vMineResults.size(); i++)
	{
		snprintf(buffer, sizeof(buffer), sHtmlResultTableRow, vMineResults[i].msg.c_str(),
			vMineResults[i].count, time_format(date, sizeof(date), vMineResults[i].time));
		out.append(buffer);
	}

	out.append(sHtmlResultBodyLow);
}

void executor::http_connection_report(std::string& out)
{
	char date[128];
	char buffer[4096];

	out.reserve(4096);

	snprintf(buffer, sizeof(buffer), sHtmlCommonHeader, "Connection Report", ver_html, "Connection Report");
	out.append(buffer);

	jpsock* pool = pick_pool_by_id(current_pool_id);

	const char* cdate = "not connected";
	if(pool != nullptr && pool->is_running() && pool->is_logged_in())
		cdate = time_format(date, sizeof(date), tPoolConnTime);

	size_t n_calls = iPoolCallTimes.size();
	unsigned int ping_time = 0;
	if(n_calls > 1)
	{
		//Not-really-but-good-enough median
		std::nth_element(iPoolCallTimes.begin(), iPoolCallTimes.begin() + n_calls / 2, iPoolCallTimes.end());
		ping_time = iPoolCallTimes[n_calls / 2];
	}

	snprintf(buffer, sizeof(buffer), sHtmlConnectionBodyHigh,
		pool != nullptr ? pool->get_rigid() : "",
		pool != nullptr ? pool->get_pool_addr() : "not connected",
		cdate, ping_time);
	out.append(buffer);

	for(size_t i = 0; i < vSocketLog.size(); i++)
	{
		snprintf(buffer, sizeof(buffer), sHtmlConnectionTableRow,
			time_format(date, sizeof(date), vSocketLog[i].time), vSocketLog[i].msg.c_str());
		out.append(buffer);
	}

	out.append(sHtmlConnectionBodyLow);
}

inline const char* hps_format_json(double h, char* buf, size_t l)
{
	if(std::isnormal(h) || h == 0.0)
	{
		snprintf(buf, l, "%.1f", h);
		return buf;
	}
	else
		return "null";
}

void executor::http_json_report(std::string& out)
{
	const char *a, *b, *c;
	char num_a[32], num_b[32], num_c[32];
	char hr_buffer[64];
	std::string hr_thds, res_error, cn_error;

	size_t nthd = pvThreads.size();
	double fTotal[3] = {0.0, 0.0, 0.0};
	hr_thds.reserve(nthd * 32);

	for(size_t i = 0; i < nthd; i++)
	{
		if(i != 0)
			hr_thds.append(1, ',');

		double fHps[3];
		fHps[0] = telem->calc_telemetry_data(10000, i);
		fHps[1] = telem->calc_telemetry_data(60000, i);
		fHps[2] = telem->calc_telemetry_data(900000, i);

		fTotal[0] += fHps[0];
		fTotal[1] += fHps[1];
		fTotal[2] += fHps[2];

		a = hps_format_json(fHps[0], num_a, sizeof(num_a));
		b = hps_format_json(fHps[1], num_b, sizeof(num_b));
		c = hps_format_json(fHps[2], num_c, sizeof(num_c));
		snprintf(hr_buffer, sizeof(hr_buffer), sJsonApiThdHashrate, a, b, c);
		hr_thds.append(hr_buffer);
	}

	a = hps_format_json(fTotal[0], num_a, sizeof(num_a));
	b = hps_format_json(fTotal[1], num_b, sizeof(num_b));
	c = hps_format_json(fTotal[2], num_c, sizeof(num_c));
	snprintf(hr_buffer, sizeof(hr_buffer), sJsonApiThdHashrate, a, b, c);

	a = hps_format_json(fHighestHps, num_a, sizeof(num_a));

	size_t iGoodRes = vMineResults[0].count, iTotalRes = iGoodRes;
	size_t ln = vMineResults.size();

	for(size_t i = 1; i < ln; i++)
		iTotalRes += vMineResults[i].count;

	jpsock* pool = pick_pool_by_id(current_pool_id);

	size_t iConnSec = 0;
	if(pool != nullptr && pool->is_running() && pool->is_logged_in())
	{
		using namespace std::chrono;
		iConnSec = duration_cast<seconds>(system_clock::now() - tPoolConnTime).count();
	}

	double fAvgResTime = 0.0;
	if(iPoolCallTimes.size() > 0)
		fAvgResTime = double(iConnSec) / iPoolCallTimes.size();

	char buffer[2048];
	res_error.reserve((vMineResults.size() - 1) * 128);
	for(size_t i = 1; i < vMineResults.size(); i++)
	{
		using namespace std::chrono;
		if(i != 1)
			res_error.append(1, ',');

		snprintf(buffer, sizeof(buffer), sJsonApiResultError, vMineResults[i].count,
			duration_cast<seconds>(vMineResults[i].time.time_since_epoch()).count(),
			vMineResults[i].msg.c_str());
		res_error.append(buffer);
	}

	size_t n_calls = iPoolCallTimes.size();
	size_t iPoolPing = 0;
	if(n_calls > 1)
	{
		//Not-really-but-good-enough median
		std::nth_element(iPoolCallTimes.begin(), iPoolCallTimes.begin() + n_calls / 2, iPoolCallTimes.end());
		iPoolPing = iPoolCallTimes[n_calls / 2];
	}

	cn_error.reserve(vSocketLog.size() * 256);
	for(size_t i = 0; i < vSocketLog.size(); i++)
	{
		using namespace std::chrono;
		if(i != 0)
			cn_error.append(1, ',');

		snprintf(buffer, sizeof(buffer), sJsonApiConnectionError,
			duration_cast<seconds>(vSocketLog[i].time.time_since_epoch()).count(),
			vSocketLog[i].msg.c_str());
		cn_error.append(buffer);
	}

	size_t bb_size = 2048 + hr_thds.size() + res_error.size() + cn_error.size();
	std::vector<char> bigbuf(bb_size);

	int bb_len = snprintf(bigbuf.data(), bb_size, sJsonApiFormat,
		get_version_str().c_str(), hr_thds.c_str(), hr_buffer, a,
		iPoolDiff, iGoodRes, iTotalRes, fAvgResTime, iPoolHashes,
		iTopDiff[0], iTopDiff[1], iTopDiff[2], iTopDiff[3], iTopDiff[4],
		iTopDiff[5], iTopDiff[6], iTopDiff[7], iTopDiff[8], iTopDiff[9],
		res_error.c_str(), pool != nullptr ? pool->get_pool_addr() : "not connected", iConnSec, iPoolPing, cn_error.c_str());

	out = std::string(bigbuf.data(), bigbuf.data() + bb_len);
}

void executor::http_report(ex_event_name ev)
{
	assert(pHttpString != nullptr);

	switch(ev)
	{
	case EV_HTML_HASHRATE:
		http_hashrate_report(*pHttpString);
		break;

	case EV_HTML_RESULTS:
		http_result_report(*pHttpString);
		break;

	case EV_HTML_CONNSTAT:
		http_connection_report(*pHttpString);
		break;

	case EV_HTML_JSON:
		http_json_report(*pHttpString);
		break;

	case EV_API_STATUS:
		api_status_report(*pHttpString);
		break;

	case EV_API_HASHRATE:
		api_hashrate_report(*pHttpString);
		break;

	case EV_API_HASHRATE_HISTORY:
		api_hashrate_history_report(*pHttpString);
		break;

	case EV_API_GPUS:
		api_gpus_report(*pHttpString);
		break;

	case EV_API_POOL:
		api_pool_report(*pHttpString);
		break;

	case EV_API_VERSION:
		api_version_report(*pHttpString);
		break;

	default:
		assert(false);
		break;
	}

	httpReady.set_value();
}

void executor::get_http_report(ex_event_name ev_id, std::string& data)
{
	std::lock_guard<std::mutex> lck(httpMutex);

	assert(pHttpString == nullptr);
	assert(ev_id == EV_HTML_HASHRATE || ev_id == EV_HTML_RESULTS || ev_id == EV_HTML_CONNSTAT || ev_id == EV_HTML_JSON ||
		ev_id == EV_API_STATUS || ev_id == EV_API_HASHRATE || ev_id == EV_API_HASHRATE_HISTORY ||
		ev_id == EV_API_GPUS || ev_id == EV_API_POOL || ev_id == EV_API_VERSION);

	pHttpString = &data;
	httpReady = std::promise<void>();
	std::future<void> ready = httpReady.get_future();

	push_event(ex_event(ev_id));

	ready.wait();
	pHttpString = nullptr;
}
