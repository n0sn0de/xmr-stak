#include "autotune_persist.hpp"

#include "n0s/vendor/rapidjson/document.h"
#include "n0s/vendor/rapidjson/prettywriter.h"
#include "n0s/vendor/rapidjson/stringbuffer.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace n0s
{
namespace autotune
{

using namespace rapidjson;

namespace
{

const char* backendToString(BackendType b)
{
	return b == BackendType::OpenCL ? "opencl" : "cuda";
}

BackendType stringToBackend(const char* s)
{
	return (std::string(s) == "cuda") ? BackendType::CUDA : BackendType::OpenCL;
}

const char* statusToString(CandidateStatus s)
{
	switch(s)
	{
	case CandidateStatus::Pending: return "pending";
	case CandidateStatus::Running: return "running";
	case CandidateStatus::Success: return "success";
	case CandidateStatus::Failed: return "failed";
	case CandidateStatus::Rejected: return "rejected";
	case CandidateStatus::Skipped: return "skipped";
	}
	return "unknown";
}

CandidateStatus stringToStatus(const char* s)
{
	std::string str(s);
	if(str == "success") return CandidateStatus::Success;
	if(str == "failed") return CandidateStatus::Failed;
	if(str == "rejected") return CandidateStatus::Rejected;
	if(str == "skipped") return CandidateStatus::Skipped;
	if(str == "running") return CandidateStatus::Running;
	return CandidateStatus::Pending;
}

void writeFingerprint(Writer<StringBuffer>& w, const DeviceFingerprint& fp)
{
	w.Key("fingerprint");
	w.StartObject();
	w.Key("backend"); w.String(backendToString(fp.backend));
	w.Key("gpu_name"); w.String(fp.gpu_name.c_str());
	w.Key("gpu_architecture"); w.String(fp.gpu_architecture.c_str());
	w.Key("vram_bytes"); w.Uint64(fp.vram_bytes);
	w.Key("compute_units"); w.Uint(fp.compute_units);
	w.Key("driver_version"); w.String(fp.driver_version.c_str());
	w.Key("runtime_version"); w.String(fp.runtime_version.c_str());
	w.Key("miner_version"); w.String(fp.miner_version.c_str());
	w.Key("algorithm"); w.String(fp.algorithm.c_str());
	w.EndObject();
}

void readFingerprint(const Value& obj, DeviceFingerprint& fp)
{
	if(!obj.HasMember("fingerprint")) return;
	const auto& f = obj["fingerprint"];
	if(f.HasMember("backend")) fp.backend = stringToBackend(f["backend"].GetString());
	if(f.HasMember("gpu_name")) fp.gpu_name = f["gpu_name"].GetString();
	if(f.HasMember("gpu_architecture")) fp.gpu_architecture = f["gpu_architecture"].GetString();
	if(f.HasMember("vram_bytes")) fp.vram_bytes = f["vram_bytes"].GetUint64();
	if(f.HasMember("compute_units")) fp.compute_units = f["compute_units"].GetUint();
	if(f.HasMember("driver_version")) fp.driver_version = f["driver_version"].GetString();
	if(f.HasMember("runtime_version")) fp.runtime_version = f["runtime_version"].GetString();
	if(f.HasMember("miner_version")) fp.miner_version = f["miner_version"].GetString();
	if(f.HasMember("algorithm")) fp.algorithm = f["algorithm"].GetString();
}

void writeMetrics(Writer<StringBuffer>& w, const BenchmarkMetrics& m)
{
	w.Key("metrics");
	w.StartObject();
	w.Key("avg_hashrate"); w.Double(m.avg_hashrate);
	w.Key("min_hashrate"); w.Double(m.min_hashrate);
	w.Key("max_hashrate"); w.Double(m.max_hashrate);
	w.Key("cv_percent"); w.Double(m.cv_percent);
	w.Key("valid_results"); w.Uint(m.valid_results);
	w.Key("invalid_results"); w.Uint(m.invalid_results);
	w.Key("backend_errors"); w.Uint(m.backend_errors);
	w.Key("benchmark_seconds"); w.Double(m.benchmark_seconds);
	w.Key("stability_seconds"); w.Double(m.stability_seconds);
	if(m.power_watts > 0.0) { w.Key("power_watts"); w.Double(m.power_watts); }
	w.EndObject();
}

void readMetrics(const Value& obj, BenchmarkMetrics& m)
{
	if(!obj.HasMember("metrics")) return;
	const auto& v = obj["metrics"];
	if(v.HasMember("avg_hashrate")) m.avg_hashrate = v["avg_hashrate"].GetDouble();
	if(v.HasMember("min_hashrate")) m.min_hashrate = v["min_hashrate"].GetDouble();
	if(v.HasMember("max_hashrate")) m.max_hashrate = v["max_hashrate"].GetDouble();
	if(v.HasMember("cv_percent")) m.cv_percent = v["cv_percent"].GetDouble();
	if(v.HasMember("valid_results")) m.valid_results = v["valid_results"].GetUint();
	if(v.HasMember("invalid_results")) m.invalid_results = v["invalid_results"].GetUint();
	if(v.HasMember("backend_errors")) m.backend_errors = v["backend_errors"].GetUint();
	if(v.HasMember("benchmark_seconds")) m.benchmark_seconds = v["benchmark_seconds"].GetDouble();
	if(v.HasMember("stability_seconds")) m.stability_seconds = v["stability_seconds"].GetDouble();
	if(v.HasMember("power_watts")) m.power_watts = v["power_watts"].GetDouble();
}

void writeScore(Writer<StringBuffer>& w, const CandidateScore& s)
{
	w.Key("score");
	w.StartObject();
	w.Key("raw_hashrate"); w.Double(s.raw_hashrate);
	w.Key("stability_penalty"); w.Double(s.stability_penalty);
	w.Key("error_penalty"); w.Double(s.error_penalty);
	w.Key("final_score"); w.Double(s.final_score);
	w.EndObject();
}

void readScore(const Value& obj, CandidateScore& s)
{
	if(!obj.HasMember("score")) return;
	const auto& v = obj["score"];
	if(v.HasMember("raw_hashrate")) s.raw_hashrate = v["raw_hashrate"].GetDouble();
	if(v.HasMember("stability_penalty")) s.stability_penalty = v["stability_penalty"].GetDouble();
	if(v.HasMember("error_penalty")) s.error_penalty = v["error_penalty"].GetDouble();
	if(v.HasMember("final_score")) s.final_score = v["final_score"].GetDouble();
}

} // anonymous namespace

bool saveAutotuneResult(const AutotuneResult& result, const std::string& filepath)
{
	StringBuffer sb;
	PrettyWriter<StringBuffer> w(sb);

	w.StartObject();
	w.Key("miner_version"); w.String(result.miner_version.c_str());
	w.Key("timestamp"); w.String(result.timestamp.c_str());

	w.Key("devices");
	w.StartArray();
	for(const auto& dev : result.devices)
	{
		w.StartObject();
		w.Key("device_index"); w.Uint(dev.device_index);
		writeFingerprint(w, dev.fingerprint);
		w.Key("mode"); w.String(dev.mode == TuneMode::Quick ? "quick" : (dev.mode == TuneMode::Balanced ? "balanced" : "exhaustive"));
		w.Key("target"); w.String(dev.target == TuneTarget::Hashrate ? "hashrate" : (dev.target == TuneTarget::Efficiency ? "efficiency" : "balanced"));
		w.Key("completed"); w.Bool(dev.completed);
		w.Key("best_candidate_id"); w.Int(dev.best_candidate_id);
		w.Key("total_elapsed_seconds"); w.Double(dev.total_elapsed_seconds);
		w.Key("timestamp"); w.String(dev.timestamp.c_str());

		w.Key("candidates");
		w.StartArray();
		for(const auto& c : dev.candidates)
		{
			w.StartObject();
			w.Key("id"); w.Uint(c.candidate_id);
			w.Key("status"); w.String(statusToString(c.status));
			if(!c.reject_reason.empty()) { w.Key("reject_reason"); w.String(c.reject_reason.c_str()); }

			if(dev.fingerprint.backend == BackendType::OpenCL)
			{
				w.Key("intensity"); w.Uint64(c.amd.intensity);
				w.Key("worksize"); w.Uint64(c.amd.worksize);
			}
			else
			{
				w.Key("threads"); w.Uint(c.nvidia.threads);
				w.Key("blocks"); w.Uint(c.nvidia.blocks);
				w.Key("bfactor"); w.Uint(c.nvidia.bfactor);
			}

			writeMetrics(w, c.metrics);
			writeScore(w, c.score);
			w.EndObject();
		}
		w.EndArray();

		w.EndObject();
	}
	w.EndArray();

	w.EndObject();

	FILE* f = fopen(filepath.c_str(), "w");
	if(!f) return false;
	fwrite(sb.GetString(), 1, sb.GetSize(), f);
	fclose(f);
	return true;
}

bool loadAutotuneResult(AutotuneResult& result, const std::string& filepath)
{
	std::ifstream ifs(filepath);
	if(!ifs.is_open()) return false;

	std::stringstream ss;
	ss << ifs.rdbuf();
	std::string content = ss.str();

	Document doc;
	if(doc.Parse(content.c_str()).HasParseError()) return false;

	if(doc.HasMember("miner_version")) result.miner_version = doc["miner_version"].GetString();
	if(doc.HasMember("timestamp")) result.timestamp = doc["timestamp"].GetString();

	if(!doc.HasMember("devices") || !doc["devices"].IsArray()) return false;

	for(const auto& d : doc["devices"].GetArray())
	{
		AutotuneState state;
		if(d.HasMember("device_index")) state.device_index = d["device_index"].GetUint();
		readFingerprint(d, state.fingerprint);

		if(d.HasMember("mode"))
		{
			std::string m = d["mode"].GetString();
			if(m == "quick") state.mode = TuneMode::Quick;
			else if(m == "exhaustive") state.mode = TuneMode::Exhaustive;
			else state.mode = TuneMode::Balanced;
		}
		if(d.HasMember("target"))
		{
			std::string t = d["target"].GetString();
			if(t == "efficiency") state.target = TuneTarget::Efficiency;
			else if(t == "balanced") state.target = TuneTarget::Balanced;
			else state.target = TuneTarget::Hashrate;
		}
		if(d.HasMember("completed")) state.completed = d["completed"].GetBool();
		if(d.HasMember("best_candidate_id")) state.best_candidate_id = d["best_candidate_id"].GetInt();
		if(d.HasMember("total_elapsed_seconds")) state.total_elapsed_seconds = d["total_elapsed_seconds"].GetDouble();
		if(d.HasMember("timestamp")) state.timestamp = d["timestamp"].GetString();

		if(d.HasMember("candidates") && d["candidates"].IsArray())
		{
			for(const auto& c : d["candidates"].GetArray())
			{
				CandidateRecord rec;
				if(c.HasMember("id")) rec.candidate_id = c["id"].GetUint();
				if(c.HasMember("status")) rec.status = stringToStatus(c["status"].GetString());
				if(c.HasMember("reject_reason")) rec.reject_reason = c["reject_reason"].GetString();

				if(state.fingerprint.backend == BackendType::OpenCL)
				{
					if(c.HasMember("intensity")) rec.amd.intensity = c["intensity"].GetUint64();
					if(c.HasMember("worksize")) rec.amd.worksize = c["worksize"].GetUint64();
				}
				else
				{
					if(c.HasMember("threads")) rec.nvidia.threads = c["threads"].GetUint();
					if(c.HasMember("blocks")) rec.nvidia.blocks = c["blocks"].GetUint();
					if(c.HasMember("bfactor")) rec.nvidia.bfactor = c["bfactor"].GetUint();
				}

				readMetrics(c, rec.metrics);
				readScore(c, rec.score);
				state.candidates.push_back(rec);
			}
		}

		result.devices.push_back(state);
	}

	return true;
}

const AutotuneState* findCachedState(const AutotuneResult& result, const DeviceFingerprint& fingerprint)
{
	for(const auto& dev : result.devices)
	{
		if(dev.fingerprint.isCompatible(fingerprint) && dev.completed)
			return &dev;
	}
	return nullptr;
}

} // namespace autotune
} // namespace n0s
