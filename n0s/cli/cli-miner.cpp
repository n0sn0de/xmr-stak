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

#include "n0s/backend/backendConnector.hpp"
#include "n0s/backend/globalStates.hpp"
#include "n0s/backend/miner_work.hpp"
#include "n0s/jconf.hpp"
#include "n0s/misc/configEditor.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/misc/executor.hpp"
#include "n0s/misc/utility.hpp"
#include "n0s/params.hpp"
#include "n0s/version.hpp"

#ifndef CONF_NO_HTTPD
#include "n0s/http/httpd.hpp"
#endif

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cinttypes>
#include <cmath>
#include <ctime>

#ifndef CONF_NO_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif



int do_benchmark(int block_version, int wait_sec, int work_sec);

void help()
{
	using namespace std;
	using namespace n0s;

	cout << "Usage: " << params::inst().binaryName << " [OPTION]..." << endl;
	cout << " " << endl;
	cout << "  -h, --help                 show this help" << endl;
	cout << "  -v, --version              show version number" << endl;
	cout << "  -V, --version-long         show long version number" << endl;
	cout << "  -c, --config FILE          common miner configuration file" << endl;
	cout << "  -C, --poolconf FILE        pool configuration file" << endl;
	cout << "  --benchmark BLOCKVERSION   ONLY do a benchmark and exit" << endl;
	cout << "  --benchwait WAIT_SEC             ... benchmark wait time" << endl;
	cout << "  --benchwork WORK_SEC             ... benchmark work time" << endl;
	cout << "  --benchmark-json FILE            ... write benchmark results as JSON" << endl;
	cout << "  --profile                        enable per-kernel timing (use with --benchmark)" << endl;
#ifndef CONF_NO_OPENCL
	cout << "  --noAMD                    disable the AMD miner backend" << endl;
	cout << "  --amdGpus GPUS             indices of AMD GPUs to use. Example: 0,2,3" << endl;
	cout << "  --noAMDCache               disable the AMD(OpenCL) cache for precompiled binaries" << endl;
	cout << "  --openCLVendor VENDOR      use OpenCL driver of VENDOR and devices [AMD,NVIDIA]" << endl;
	cout << "                             default: AMD" << endl;
	cout << "  --amdCacheDir DIRECTORY    directory to store AMD binary files" << endl;
	cout << "  --amd FILE                 AMD backend miner config file" << endl;
#endif
#ifndef CONF_NO_CUDA
	cout << "  --noNVIDIA                 disable the NVIDIA miner backend" << endl;
	cout << "  --nvidiaGpus GPUS          indices of NVIDIA GPUs to use. Example: 0,2,3" << endl;
	cout << "  --nvidia FILE              NVIDIA backend miner config file" << endl;
#endif
	cout << "  --log FILE                 miner output file" << endl;
	cout << "  --h-print-time SEC         interval for printing hashrate, in seconds" << endl;
#ifndef CONF_NO_HTTPD
	cout << "  -i --httpd HTTP_PORT       HTTP interface port" << endl;
#endif
	cout << " " << endl;
	cout << "The following options can be used for automatic start without a guided config," << endl;
	cout << "If config exists then this pool will be top priority." << endl;
	cout << "  -o, --url URL              pool url and port, e.g. pool.usxmrpool.com:3333" << endl;
	cout << "  -O, --tls-url URL          TLS pool url and port, e.g. pool.usxmrpool.com:10443" << endl;
	cout << "  -u, --user USERNAME        pool user name or wallet address" << endl;
	cout << "  -r, --rigid RIGID          rig identifier for pool-side statistics (needs pool support)" << endl;
	cout << "  -p, --pass PASSWD          pool password, in the most cases x or empty \"\"" << endl;
	cout << "  --use-nicehash             the pool should run in nicehash mode" << endl;
	cout << endl;
	std::string algos;
	jconf::GetAlgoList(algos);
	cout << "Supported coin options: " << endl
		 << algos << endl;
	cout << "Version: " << get_version_str_short() << endl;
	cout << "n0s-ryo-miner - Optimized for RYO Currency (CryptoNight-GPU)" << endl;
	cout << "Based on xmr-stak by fireice_uk and psychocrypt (GPLv3)" << endl;
}

bool read_yes_no(const char* str, std::string default_value = "")
{
	std::string tmp;
	do
	{
		std::cout << str << std::endl;
		getline(std::cin, tmp);
		if(tmp.empty())
			tmp = default_value;
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
	} while(tmp != "y" && tmp != "n" && tmp != "yes" && tmp != "no");

	return tmp == "y" || tmp == "yes";
}

inline const char* bool_to_str(bool v)
{
	return v ? "true" : "false";
}

std::string get_multipool_entry(bool& final)
{
	std::cout << std::endl
			  << "- Next Pool:" << std::endl
			  << std::endl;

	std::string pool;
	std::cout << "- Pool address: e.g. " << jconf::GetDefaultPool(n0s::params::inst().currency.c_str()) << std::endl;
	std::cin >> pool;

	std::string userName;
	std::cout << "- Username (wallet address or pool login):" << std::endl;
	std::cin >> userName;

	std::string passwd;
	std::cin.clear();
	std::cin.ignore(INT_MAX, '\n');
	std::cout << "- Password (mostly empty or x):" << std::endl;
	getline(std::cin, passwd);

	std::string rigid;
	std::cout << "- Rig identifier for pool-side statistics (needs pool support). Can be empty:" << std::endl;
	getline(std::cin, rigid);

#ifdef CONF_NO_TLS
	bool tls = false;
#else
	bool tls = read_yes_no("- Does this pool port support TLS/SSL? Use no if unknown. (y/N)", "N");
#endif
	bool nicehash = read_yes_no("- Do you want to use nicehash on this pool? (y/N)", "N");

	int64_t pool_weight;
	std::cout << "- Please enter a weight for this pool: " << std::endl;
	while(!(std::cin >> pool_weight) || pool_weight <= 0)
	{
		std::cin.clear();
		std::cin.ignore(INT_MAX, '\n');
		std::cout << "Invalid weight.  Try 1, 10, 100, etc:" << std::endl;
	}

	final = !read_yes_no("- Do you want to add another pool? (y/N)", "N");

	return "\t{\"pool_address\" : \"" + pool + "\", \"wallet_address\" : \"" + userName + "\", \"rig_id\" : \"" + rigid +
		   "\", \"pool_password\" : \"" + passwd + "\", \"use_nicehash\" : " + bool_to_str(nicehash) + ", \"use_tls\" : " +
		   bool_to_str(tls) + ", \"tls_fingerprint\" : \"\", \"pool_weight\" : " + std::to_string(pool_weight) + " },\n";
}

inline void prompt_once(bool& prompted)
{
	if(!prompted)
	{
		std::cout << "Please enter:" << std::endl;
		prompted = true;
	}
}

inline bool use_simple_start()
{
	// ask this question only once
	static bool simple_start = read_yes_no("\nUse simple setup method? (Y/n)", "Y");
	return simple_start;
}

void do_guided_pool_config()
{
	using namespace n0s;

	// load the template of the backend config into a char variable
	const char* tpl =
#include "../pools.tpl"
		;

	configEditor configTpl{};
	configTpl.set(std::string(tpl));
	bool prompted = false;

	// Hardcoded to cryptonight_gpu (RYO Currency)
	auto currency = params::inst().currency;
	if(currency.empty())
		currency = "cryptonight_gpu";

	auto pool = params::inst().poolURL;
	bool userSetPool = true;
	if(pool.empty())
	{
		prompt_once(prompted);

		userSetPool = false;
		std::cout << "- Pool address: e.g. " << jconf::GetDefaultPool(n0s::params::inst().currency.c_str()) << std::endl;
		std::cin >> pool;
	}

	auto userName = params::inst().poolUsername;
	if(userName.empty())
	{
		prompt_once(prompted);

		std::cout << "- Username (wallet address or pool login):" << std::endl;
		std::cin >> userName;
	}

	bool stdin_flushed = false;
	auto passwd = params::inst().poolPasswd;
	if(passwd.empty() && !params::inst().userSetPwd)
	{
		prompt_once(prompted);

		// clear everything from stdin to allow an empty password
		std::cin.clear();
		std::cin.ignore(INT_MAX, '\n');
		stdin_flushed = true;

		std::cout << "- Password (mostly empty or x):" << std::endl;
		getline(std::cin, passwd);
	}

	auto rigid = params::inst().poolRigid;
	if(rigid.empty() && !params::inst().userSetRigid)
	{
		if(!use_simple_start())
		{
			prompt_once(prompted);

			if(!stdin_flushed)
			{
				// clear everything from stdin to allow an empty rigid
				std::cin.clear();
				std::cin.ignore(INT_MAX, '\n');
			}

			std::cout << "- Rig identifier for pool-side statistics (needs pool support). Can be empty:" << std::endl;
			getline(std::cin, rigid);
		}
	}

	bool tls = params::inst().poolUseTls;
#ifdef CONF_NO_TLS
	tls = false;
#else
	if(!userSetPool)
	{
		prompt_once(prompted);
		tls = read_yes_no("- Does this pool port support TLS/SSL? Use no if unknown. (y/N)", "N");
	}

#endif

	bool nicehash = params::inst().nicehashMode;
	if(!userSetPool)
	{
		if(!use_simple_start())
		{
			prompt_once(prompted);
			nicehash = read_yes_no("- Do you want to use nicehash on this pool? (y/N)", "N");
		}
	}

	bool multipool = false;
	if(!userSetPool)
		if(!use_simple_start())
			multipool = read_yes_no("- Do you want to use multiple pools? (y/N)", "N");

	int64_t pool_weight = 1;
	if(multipool)
	{
		std::cout << "Pool weight is a number telling the miner how important the pool is." << std::endl;
		std::cout << "Miner will mine mostly at the pool with the highest weight, unless the pool fails." << std::endl;
		std::cout << "Weight must be an integer larger than 0." << std::endl;
		std::cout << "- Please enter a weight for this pool: " << std::endl;

		while(!(std::cin >> pool_weight) || pool_weight <= 0)
		{
			std::cin.clear();
			std::cin.ignore(INT_MAX, '\n');
			std::cout << "Invalid weight.  Try 1, 10, 100, etc:" << std::endl;
		}
	}

	std::string pool_table;
	pool_table += "\t{\"pool_address\" : \"" + pool + "\", \"wallet_address\" : \"" + userName + "\", \"rig_id\" : \"" + rigid +
				  "\", \"pool_password\" : \"" + passwd + "\", \"use_nicehash\" : " + bool_to_str(nicehash) + ", \"use_tls\" : " +
				  bool_to_str(tls) + ", \"tls_fingerprint\" : \"\", \"pool_weight\" : " + std::to_string(pool_weight) + " },\n";

	if(multipool)
	{
		bool final;
		do
		{
			pool_table += get_multipool_entry(final);
		} while(!final);
	}

	configTpl.replace("CURRENCY", currency);
	configTpl.replace("POOLCONF", pool_table);
	configTpl.write(params::inst().configFilePools);
	std::cout << "Pool configuration stored in file '" << params::inst().configFilePools << "'" << std::endl;
}

void do_guided_config()
{
	using namespace n0s;

	// load the template of the backend config into a char variable
	const char* tpl =
#include "../config.tpl"
		;

	configEditor configTpl{};
	configTpl.set(std::string(tpl));
	bool prompted = false;

	auto http_port = params::inst().httpd_port;
	if(http_port == params::httpd_port_unset)
	{
		http_port = params::httpd_port_disabled;
#ifndef CONF_NO_HTTPD
		if(!use_simple_start())
		{
			prompt_once(prompted);

			std::cout << "- Do you want to use the HTTP interface?" << std::endl;
			std::cout << "Unlike the screen display, browser interface is not affected by the GPU lag." << std::endl;
			std::cout << "If you don't want to use it, please enter 0, otherwise enter port number that the miner should listen on" << std::endl;

			int32_t port;
			while(!(std::cin >> port) || port < 0 || port > 65535)
			{
				std::cin.clear();
				std::cin.ignore(INT_MAX, '\n');
				std::cout << "Invalid port number. Please enter a number between 0 and 65535." << std::endl;
			}
			http_port = port;
		}
#endif
	}

	configTpl.replace("HTTP_PORT", std::to_string(http_port));
	configTpl.replace("OUTPUT_FILE", params::inst().outputFile);
	configTpl.replace("H_PRINT_TIME", std::to_string(params::inst().h_print_time > 0 ? params::inst().h_print_time : 300));
	configTpl.write(params::inst().configFile);
	std::cout << "Configuration stored in file '" << params::inst().configFile << "'" << std::endl;
}

int main(int argc, char* argv[])
{
#ifndef CONF_NO_TLS
	// OpenSSL 3.0+: single call replaces all legacy init functions
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
	OpenSSL_add_all_digests();
#endif

	srand(time(0));

	using namespace n0s;

	const size_t argc_sz = static_cast<size_t>(argc);

	std::string pathWithName(argv[0]);
	std::string separator("/");
	auto pos = pathWithName.rfind(separator);

	if(pos == std::string::npos)
	{
		// try windows "\"
		separator = "\\";
		pos = pathWithName.rfind(separator);
	}
	params::inst().binaryName = std::string(pathWithName, pos + 1, std::string::npos);
	if(params::inst().binaryName.compare(pathWithName) != 0)
	{
		params::inst().executablePrefix = std::string(pathWithName, 0, pos);
		params::inst().executablePrefix += separator;
	}

	params::inst().minerArg0 = argv[0];
	params::inst().minerArgs.reserve(argc_sz * 16);
	for(size_t i = 1; i < argc_sz; i++)
	{
		params::inst().minerArgs += " ";
		params::inst().minerArgs += argv[i];
	}

	bool pool_url_set = false;
	for(size_t i = 1; i < argc_sz - 1; i++)
	{
		std::string opName(argv[i]);
		if(opName == "-o" || opName == "-O" || opName == "--url" || opName == "--tls-url")
			pool_url_set = true;
	}

	for(size_t i = 1; i < argc_sz; ++i)
	{
		std::string opName(argv[i]);
		if(opName.compare("-h") == 0 || opName.compare("--help") == 0)
		{
			help();
			n0s_exit(0);
			return 0;
		}
		if(opName.compare("-v") == 0 || opName.compare("--version") == 0)
		{
			std::cout << "Version: " << get_version_str_short() << std::endl;
			n0s_exit();
			return 0;
		}
		else if(opName.compare("-V") == 0 || opName.compare("--version-long") == 0)
		{
			std::cout << "Version: " << get_version_str() << std::endl;
			n0s_exit();
			return 0;
		}
		else if(opName.compare("--noAMD") == 0)
		{
			params::inst().useAMD = false;
		}
		else if (opName.compare("--amdGpus") == 0)
		{
			++i;
			if (i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--amdGpus' given");
				n0s_exit();
				return 1;
			}
			params::inst().amdGpus = argv[i];
		}
		else if(opName.compare("--openCLVendor") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--openCLVendor' given");
				n0s_exit();
				return 1;
			}
			std::string vendor(argv[i]);
			params::inst().openCLVendor = vendor;
			if(vendor != "AMD" && vendor != "NVIDIA")
			{
				printer::inst()->print_msg(L0, "'--openCLVendor' must be 'AMD' or 'NVIDIA'");
				n0s_exit();
				return 1;
			}
		}
		else if(opName.compare("--noAMDCache") == 0)
		{
			params::inst().AMDCache = false;
		}
		else if(opName.compare("--noNVIDIA") == 0)
		{
			params::inst().useNVIDIA = false;
		}
		else if (opName.compare("--nvidiaGpus") == 0)
		{
			++i;
			if (i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--nvidiaGpus' given");
				n0s_exit();
				return 1;
			}
			params::inst().nvidiaGpus = argv[i];
		}
		else if(opName.compare("--amd") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--amd' given");
				n0s_exit();
				return 1;
			}
			params::inst().configFileAMD = argv[i];
		}
		else if(opName.compare("--amdCacheDir") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--amdCacheDir' given");
				n0s_exit();
				return 1;
			}
			params::inst().rootAMDCacheDir = std::string(argv[i]) + "/";
		}
		else if(opName.compare("--nvidia") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--nvidia' given");
				n0s_exit();
				return 1;
			}
			params::inst().configFileNVIDIA = argv[i];
		}
		else if(opName.compare("--currency") == 0)
		{
			++i; // consume argument for backward compatibility
			if(i < argc_sz)
				printer::inst()->print_msg(L0, "WARNING: --currency is deprecated. Algorithm is hardcoded to cryptonight_gpu.");
		}
		else if(opName.compare("-o") == 0 || opName.compare("--url") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-o/--url' given");
				n0s_exit();
				return 1;
			}
			params::inst().poolURL = argv[i];
			params::inst().poolUseTls = false;
		}
		else if(opName.compare("-O") == 0 || opName.compare("--tls-url") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-O/--tls-url' given");
				n0s_exit();
				return 1;
			}
			params::inst().poolURL = argv[i];
			params::inst().poolUseTls = true;
		}
		else if(opName.compare("-u") == 0 || opName.compare("--user") == 0)
		{
			if(!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify username and password.");
				n0s_exit();
				return 1;
			}

			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-u/--user' given");
				n0s_exit();
				return 1;
			}
			params::inst().poolUsername = argv[i];
		}
		else if(opName.compare("-p") == 0 || opName.compare("--pass") == 0)
		{
			if(!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify username and password.");
				n0s_exit();
				return 1;
			}

			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-p/--pass' given");
				n0s_exit();
				return 1;
			}
			params::inst().userSetPwd = true;
			params::inst().poolPasswd = argv[i];
		}
		else if(opName.compare("-r") == 0 || opName.compare("--rigid") == 0)
		{
			if(!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify rigid.");
				n0s_exit();
				return 1;
			}

			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-r/--rigid' given");
				n0s_exit();
				return 1;
			}

			params::inst().userSetRigid = true;
			params::inst().poolRigid = argv[i];
		}
		else if(opName.compare("--use-nicehash") == 0)
		{
			params::inst().nicehashMode = true;
		}
		else if(opName.compare("-c") == 0 || opName.compare("--config") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-c/--config' given");
				n0s_exit();
				return 1;
			}
			params::inst().configFile = argv[i];
		}
		else if(opName.compare("-C") == 0 || opName.compare("--poolconf") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-C/--poolconf' given");
				n0s_exit();
				return 1;
			}
			params::inst().configFilePools = argv[i];
		}
		else if(opName.compare("--log") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--log' given");
				n0s_exit();
				return 1;
			}
			params::inst().outputFile = argv[i];
		}
		else if (opName.compare("--h-print-time") == 0)
		{
			++i;
			if (i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--h-print-time' given");
				n0s_exit();
				return 1;
			}
			char* h_print_time = nullptr;
			long int time = strtol(argv[i], &h_print_time, 10);

			if (time <= 0)
			{
				printer::inst()->print_msg(L0, "Hashrate print time must be > 0");
				return 1;
			}
			params::inst().h_print_time = time;
		}
		else if(opName.compare("-i") == 0 || opName.compare("--httpd") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-i/--httpd' given");
				n0s_exit();
				return 1;
			}

			char* endp = nullptr;
			long int ret = strtol(argv[i], &endp, 10);

			if(endp == nullptr || ret < 0 || ret > 65535)
			{
				printer::inst()->print_msg(L0, "Argument for parameter '-i/--httpd' must be a number between 0 and 65535");
				n0s_exit();
				return 1;
			}

			params::inst().httpd_port = ret;
		}
		else if(opName.compare("--benchmark") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchmark' given");
				n0s_exit();
				return 1;
			}
			char* block_version = nullptr;
			long int bversion = strtol(argv[i], &block_version, 10);

			if(bversion < 0 || bversion >= 256)
			{
				printer::inst()->print_msg(L0, "Benchmark block version must be in the range [0,255]");
				return 1;
			}
			params::inst().benchmark_block_version = bversion;
		}
		else if(opName.compare("--benchwait") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchwait' given");
				n0s_exit();
				return 1;
			}
			char* wait_sec = nullptr;
			long int waitsec = strtol(argv[i], &wait_sec, 10);

			if(waitsec < 0 || waitsec >= 300)
			{
				printer::inst()->print_msg(L0, "Benchmark wait seconds must be in the range [0,300]");
				return 1;
			}
			params::inst().benchmark_wait_sec = waitsec;
		}
		else if(opName.compare("--benchwork") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchwork' given");
				n0s_exit();
				return 1;
			}
			char* work_sec = nullptr;
			long int worksec = strtol(argv[i], &work_sec, 10);

			if(worksec < 10 || worksec >= 300)
			{
				printer::inst()->print_msg(L0, "Benchmark work seconds must be in the range [10,300]");
				return 1;
			}
			params::inst().benchmark_work_sec = worksec;
		}
		else if(opName.compare("--benchmark-json") == 0)
		{
			++i;
			if(i >= argc_sz)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchmark-json' given");
				n0s_exit();
				return 1;
			}
			params::inst().benchmark_json = argv[i];
		}
		else if(opName.compare("--profile") == 0)
		{
			params::inst().profileKernels = true;
		}
		else
		{
			printer::inst()->print_msg(L0, "Parameter unknown '%s'", argv[i]);
			n0s_exit();
			return 1;
		}
	}

	bool hasConfigFile = configEditor::file_exist(params::inst().configFile);
	bool hasPoolConfig = configEditor::file_exist(params::inst().configFilePools);

	// check if we need a guided start
	if(!hasConfigFile)
		do_guided_config();

	if(!hasPoolConfig)
		do_guided_pool_config();

	if(!jconf::inst()->parse_config(params::inst().configFile.c_str(), params::inst().configFilePools.c_str()))
	{
		n0s_exit();
		return 1;
	}

	// Windows UAC elevation removed — Linux-only build

	if(strlen(jconf::inst()->GetOutputFile()) != 0)
		printer::inst()->open_logfile(jconf::inst()->GetOutputFile());

	if(!BackendConnector::self_test())
	{
		printer::inst()->print_msg(L0, "Self test not passed!");
		n0s_exit();
		return 1;
	}

	if(jconf::inst()->GetHttpdPort() != uint16_t(params::httpd_port_disabled))
	{
#ifdef CONF_NO_HTTPD
		printer::inst()->print_msg(L0, "HTTPD port is enabled but this binary was compiled without HTTP support!");
		n0s_exit();
		return 1;
#else
		if(!httpd::inst()->start_daemon())
		{
			n0s_exit();
			return 1;
		}
#endif
	}

	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str(get_version_str_short().c_str());
	printer::inst()->print_str("\n\n");
	printer::inst()->print_str("n0s-ryo-miner - Optimized GPU miner for RYO Currency\n");
	printer::inst()->print_str("CryptoNight-GPU algorithm - AMD (OpenCL) & NVIDIA (CUDA) support\n\n");
	printer::inst()->print_str("Based on xmr-stak by fireice_uk and psychocrypt (GPLv3)\n");
#ifndef CONF_NO_CUDA
	printer::inst()->print_str("NVIDIA backend: KlausT and psychocrypt\n");
#endif
#ifndef CONF_NO_OPENCL
	printer::inst()->print_str("AMD backend: wolf9466\n");
#endif
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str("Keyboard commands:\n");
	printer::inst()->print_str("  'h' - hashrate\n");
	printer::inst()->print_str("  'r' - results\n");
	printer::inst()->print_str("  'c' - connection\n");
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str("Mining RYO Currency:\n");
	printer::inst()->print_str("   #####   ______               ____\n");
	printer::inst()->print_str(" ##     ## | ___ \\             /  _ \\\n");
	printer::inst()->print_str("#    _    #| |_/ /_   _   ___  | / \\/ _   _  _ _  _ _  ___  _ __    ___  _   _\n");
	printer::inst()->print_str("#   |_|   #|    /| | | | / _ \\ | |   | | | || '_|| '_|/ _ \\| '_ \\  / __|| | | |\n");
	printer::inst()->print_str("#         #| |\\ \\| |_| || (_) || \\_/\\| |_| || |  | | |  __/| | | || (__ | |_| |\n");
	printer::inst()->print_str(" ##     ## \\_| \\_|\\__, | \\___/ \\____/ \\__,_||_|  |_|  \\___||_| |_| \\___| \\__, |\n");
	printer::inst()->print_str("   #####           __/ |                                                  __/ |\n");
	printer::inst()->print_str("                  |___/   https://ryo-currency.com                       |___/\n\n");
	printer::inst()->print_str("Privacy-focused cryptocurrency with GPU-friendly mining.\n");
	printer::inst()->print_str("More info: https://github.com/ryo-currency\n");
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_msg(L0, "Mining coin: %s", ::jconf::inst()->GetMiningAlgo().Name().c_str());

	if(params::inst().benchmark_block_version >= 0)
	{
		printer::inst()->print_str("!!!! Doing only a benchmark and exiting. To mine, remove the '--benchmark' option. !!!!\n");
		return do_benchmark(params::inst().benchmark_block_version, params::inst().benchmark_wait_sec, params::inst().benchmark_work_sec);
	}

	executor::inst()->ex_start(jconf::inst()->DaemonMode());

	uint64_t lastTime = get_timestamp_ms();
	int key;
	while(true)
	{
		key = get_key();

		switch(key)
		{
		case 'h':
			executor::inst()->push_event(ex_event(EV_USR_HASHRATE));
			break;
		case 'r':
			executor::inst()->push_event(ex_event(EV_USR_RESULTS));
			break;
		case 'c':
			executor::inst()->push_event(ex_event(EV_USR_CONNSTAT));
			break;
		default:
			break;
		}

		uint64_t currentTime = get_timestamp_ms();

		/* Hard guard to make sure we never get called more than twice per second */
		if(currentTime - lastTime < 500)
			std::this_thread::sleep_for(std::chrono::milliseconds(500 - (currentTime - lastTime)));
		lastTime = currentTime;
	}

	return 0;
}

int do_benchmark(int block_version, int wait_sec, int work_sec)
{
	using namespace std::chrono;
	std::vector<std::unique_ptr<n0s::iBackend>> pvThreads;

	printer::inst()->print_msg(L0, "Prepare benchmark for block version %d", block_version);

	if(block_version <= 0)
	{
		printer::inst()->print_msg(L0, "Block version must be >0, current value is %u.", block_version);
		return 1;
	}

	alignas(16) uint8_t work[128];
	memset(work, 0, sizeof(work));
	work[0] = static_cast<uint8_t>(block_version);

	n0s::pool_data dat;

	n0s::miner_work oWork = n0s::miner_work();
	pvThreads = n0s::BackendConnector::thread_starter(oWork);

	if(pvThreads.empty())
	{
		printer::inst()->print_msg(L0, "ERROR: No mining backends started.");
		return 1;
	}

	printer::inst()->print_msg(L0, "Wait %d sec until all backends are initialized", wait_sec);
	std::this_thread::sleep_for(std::chrono::seconds(wait_sec));

	printer::inst()->print_msg(L0, "Start a %d second benchmark...", work_sec);
	constexpr uint32_t work_size = 76; // Realistic CryptoNight-GPU block size
	n0s::globalStates::inst().switch_work(n0s::miner_work("bench", work, work_size, 0, false, 1, 0), dat);
	uint64_t iStartStamp = get_timestamp_ms();

	// Sample hashrate at intervals for stability tracking
	constexpr int sample_interval_sec = 5;
	const int num_samples = std::max(1, work_sec / sample_interval_sec);
	struct thread_sample
	{
		uint64_t hash_count;
		uint64_t timestamp;
	};
	// Per-thread sample history: samples[thread_idx][sample_idx]
	std::vector<std::vector<thread_sample>> samples(pvThreads.size());
	for(auto& s : samples)
		s.reserve(static_cast<size_t>(num_samples) + 1);

	// Take initial sample
	for(size_t i = 0; i < pvThreads.size(); i++)
		samples[i].push_back({pvThreads[i]->iHashCount.load(), get_timestamp_ms()});

	for(int s = 0; s < num_samples; s++)
	{
		int sleep_time = (s < num_samples - 1) ? sample_interval_sec : (work_sec - s * sample_interval_sec);
		std::this_thread::sleep_for(std::chrono::seconds(sleep_time));

		for(size_t i = 0; i < pvThreads.size(); i++)
			samples[i].push_back({pvThreads[i]->iHashCount.load(), get_timestamp_ms()});
	}

	// Stop mining
	n0s::globalStates::inst().switch_work(n0s::miner_work("bench_stop", work, work_size, 0, false, 0, 0), dat);

	// Wait for threads to notice the stop
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	// Calculate per-thread results
	double fTotalHps = 0.0;
	bool json_output = !n0s::params::inst().benchmark_json.empty();

	// JSON output buffer
	std::string json;
	if(json_output)
	{
		json = "{\n  \"benchmark\": {\n";
		json += "    \"block_version\": " + std::to_string(block_version) + ",\n";
		json += "    \"wait_sec\": " + std::to_string(wait_sec) + ",\n";
		json += "    \"work_sec\": " + std::to_string(work_sec) + ",\n";
		json += "    \"threads\": [\n";
	}

	for(size_t i = 0; i < pvThreads.size(); i++)
	{
		auto bType = static_cast<n0s::iBackend::BackendType>(pvThreads[i]->backendType);
		std::string name(n0s::iBackend::getName(bType));

		// Overall hashrate from total hashes / total time
		auto& ts = samples[i];
		double elapsed_ms = static_cast<double>(ts.back().timestamp - ts.front().timestamp);
		uint64_t total_hashes = ts.back().hash_count - ts.front().hash_count;
		double avg_hps = (elapsed_ms > 0) ? (static_cast<double>(total_hashes) / (elapsed_ms / 1000.0)) : 0.0;

		// Per-interval hashrates for stability analysis
		std::vector<double> interval_rates;
		interval_rates.reserve(ts.size() - 1);
		for(size_t j = 1; j < ts.size(); j++)
		{
			double dt_ms = static_cast<double>(ts[j].timestamp - ts[j - 1].timestamp);
			uint64_t dh = ts[j].hash_count - ts[j - 1].hash_count;
			if(dt_ms > 0)
				interval_rates.push_back(static_cast<double>(dh) / (dt_ms / 1000.0));
		}

		// Calculate stability metrics (coefficient of variation)
		double mean = 0.0, variance = 0.0, cv = 0.0;
		if(!interval_rates.empty())
		{
			for(double r : interval_rates)
				mean += r;
			mean /= static_cast<double>(interval_rates.size());

			for(double r : interval_rates)
				variance += (r - mean) * (r - mean);
			variance /= static_cast<double>(interval_rates.size());

			cv = (mean > 0) ? (std::sqrt(variance) / mean * 100.0) : 0.0;
		}

		printer::inst()->print_msg(L0, "Thread %zu [%s]: %.1f H/s avg | CV: %.1f%% | %zu samples",
			i, name.c_str(), avg_hps, cv, interval_rates.size());
		fTotalHps += avg_hps;

		if(json_output)
		{
			if(i > 0)
				json += ",\n";
			char buf[512];
			snprintf(buf, sizeof(buf),
				"      { \"id\": %zu, \"backend\": \"%s\", \"avg_hps\": %.1f, \"cv_pct\": %.2f, \"samples\": %zu, \"total_hashes\": %" PRIu64 " }",
				i, name.c_str(), avg_hps, cv, interval_rates.size(), total_hashes);
			json += buf;
		}
	}

	printer::inst()->print_msg(L0, "Benchmark Total: %.1f H/s", fTotalHps);

	if(json_output)
	{
		char buf[256];
		snprintf(buf, sizeof(buf),
			"\n    ],\n    \"total_hps\": %.1f,\n    \"timestamp\": %" PRIu64 "\n  }\n}\n",
			fTotalHps, static_cast<uint64_t>(system_clock::to_time_t(system_clock::now())));
		json += buf;

		FILE* f = fopen(n0s::params::inst().benchmark_json.c_str(), "w");
		if(f)
		{
			fputs(json.c_str(), f);
			fclose(f);
			printer::inst()->print_msg(L0, "Benchmark results written to: %s", n0s::params::inst().benchmark_json.c_str());
		}
		else
		{
			printer::inst()->print_msg(L0, "ERROR: Could not write to: %s", n0s::params::inst().benchmark_json.c_str());
		}
	}

	return 0;
}
