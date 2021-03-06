#include "subcommand.hpp"
#include "odgi.hpp"
#include "args.hxx"
#include "algorithms/bin_path_info.hpp"

namespace odgi {

using namespace odgi::subcommand;

int main_bin(int argc, char** argv) {

    for (uint64_t i = 1; i < argc-1; ++i) {
        argv[i] = argv[i+1];
    }
    std::string prog_name = "odgi bin";
    argv[0] = (char*)prog_name.c_str();
    --argc;
    
    args::ArgumentParser parser("binning of path information in the graph");
    args::HelpFlag help(parser, "help", "display this help summary", {'h', "help"});
    args::ValueFlag<std::string> dg_out_file(parser, "FILE", "store the graph in this file", {'o', "out"});
    args::ValueFlag<std::string> dg_in_file(parser, "FILE", "load the graph from this file", {'i', "idx"});
    args::ValueFlag<std::string> path_delim(parser, "path-delim", "annotate rows by prefix and suffix of this delimiter", {'D', "path-delim"});
    args::Flag output_json(parser, "write-json", "write JSON format output including additional path positional information", {'j', "json"});
    args::Flag aggregate_delim(parser, "aggregate-delim", "aggregate on path prefix delimiter", {'a', "aggregate-delim"});
    args::ValueFlag<uint64_t> num_bins(parser, "N", "number of bins", {'n', "num-bins"});
    args::ValueFlag<uint64_t> bin_width(parser, "bp", "width of each bin in basepairs along the graph vector", {'w', "bin-width"});
    args::Flag write_seqs_not(parser, "write-seqs-not", "don't write out the sequences for each bin", {'s', "no-seqs"});
    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    if (argc==1) {
        std::cout << parser;
        return 1;
    }

    graph_t graph;
    assert(argc > 0);
    std::string infile = args::get(dg_in_file);
    if (infile.size()) {
        if (infile == "-") {
            graph.deserialize(std::cin);
        } else {
            ifstream f(infile.c_str());
            graph.deserialize(f);
            f.close();
        }
    }

    std::string delim = args::get(path_delim);
    bool agg_delim = args::get(aggregate_delim);
    auto get_path_prefix = [&](const std::string& path_name) -> std::string {
        if (agg_delim || delim.empty()) {
            return "NA";
        } else {
            return path_name.substr(0, path_name.find(delim));
        }
    };
    auto get_path_suffix = [&](const std::string& path_name) -> std::string {
        if (agg_delim || delim.empty()) {
            return "NA";
        } else {
            return path_name.substr(path_name.find(delim)+1);
        }
    };

    // our aggregation matrix
    std::vector<std::pair<std::string, std::vector<algorithms::path_info_t>>> table;
    if (args::get(num_bins) + args::get(bin_width) == 0) {
        std::cerr << "[odgi bin] error: a bin width or a bin count is required" << std::endl;
        return 1;
    }

    // ODGI JSON VERSION
    const uint64_t ODGI_JSON_VERSION = 10;

    std::function<void(const uint64_t&, const uint64_t&)> write_header_tsv
    = [&] (const uint64_t pangenome_length, const uint64_t bin_width) {
        // no header necessary for tsv so far
    };

    std::function<void(const uint64_t&,
            const uint64_t&)> write_header_json
    = [&] (const uint64_t pangenome_length, const uint64_t bin_width) {
        std::cout << "{\"odgi_version\": " << ODGI_JSON_VERSION << ",";
        std::cout << "\"bin_width\": " << bin_width << ",";
        std::cout << "\"pangenome_length\": " << pangenome_length << "}" << std::endl;
    };

    std::function<void(const uint64_t&,
                       const std::string&)> write_seq_json
        = [&](const uint64_t& bin_id, const std::string& seq) {
        if (args::get(write_seqs_not)) {
            std::cout << "{\"bin_id\":" << bin_id << "}" << std::endl;
        } else {
            std::cout << "{\"bin_id\":" << bin_id << ","
                      << "\"sequence\":\"" << seq << "\"}" << std::endl;
        }
    };

    std::function<void(const std::string&,
                       const std::vector<std::pair<uint64_t, uint64_t>>&,
                       const std::map<uint64_t, algorithms::path_info_t>&)> write_json
        = [&](const std::string& path_name,
              const std::vector<std::pair<uint64_t, uint64_t>>& links,
              const std::map<uint64_t, algorithms::path_info_t>& bins) {
        std::string name_prefix = get_path_prefix(path_name);
        std::string name_suffix = get_path_suffix(path_name);
        std::cout << "{\"path_name\":\"" << path_name << "\",";
        if (!delim.empty()) {
            std::cout << "\"path_name_prefix\":\"" << name_prefix << "\","
                      << "\"path_name_suffix\":\"" << name_suffix << "\",";
        }
        std::cout << "\"bins\":[";
        auto entry_it = bins.begin();
        for (uint64_t i = 0; i < bins.size(); ++i) {
            auto& bin_id = entry_it->first;
            auto& info = entry_it->second;
            std::cout << "[" << bin_id << ","
                      << info.mean_cov << ","
                      << info.mean_inv << ","
                      << info.mean_pos << ","
					  << info.first_nucleotide << ","
					  << info.last_nucleotide << "]";
            if (i+1 != bins.size()) {
                std::cout << ",";
            }
            ++entry_it;
        }
        std::cout << "],";
        std::cout << "\"links\":[";
        for (uint64_t i = 0; i < links.size(); ++i) {
            auto& link = links[i];
            std::cout << "[" << link.first << "," << link.second << "]";
            if (i+1 < links.size()) std::cout << ",";
        }
        std::cout << "]}" << std::endl;
    };

    std::function<void(const uint64_t&,
                       const std::string&)> write_seq_noop
        = [&](const uint64_t& bin_id, const std::string& seq) {
    };

    std::function<void(const std::string&,
                       const std::vector<std::pair<uint64_t, uint64_t>>&,
                       const std::map<uint64_t, algorithms::path_info_t>&)> write_tsv
        = [&](const std::string& path_name,
              const std::vector<std::pair<uint64_t, uint64_t>>& links,
              const std::map<uint64_t, algorithms::path_info_t>& bins) {
        std::string name_prefix = get_path_prefix(path_name);
        std::string name_suffix = get_path_suffix(path_name);
        for (auto& entry : bins) {
            auto& bin_id = entry.first;
            auto& info = entry.second;
            if (info.mean_cov) {
                std::cout << path_name << "\t"
                          << name_prefix << "\t"
                          << name_suffix << "\t"
                          << bin_id << "\t"
                          << info.mean_cov << "\t"
                          << info.mean_inv << "\t"
                          << info.mean_pos << "\t"
						  << info.first_nucleotide << "\t"
						  << info.last_nucleotide << std::endl;
            }
        }
    };

    if (args::get(output_json)) {
        algorithms::bin_path_info(graph, (args::get(aggregate_delim) ? args::get(path_delim) : ""),
                                  write_header_json,write_json, write_seq_json,
                                  args::get(num_bins), args::get(bin_width));
    } else {
        std::cout << "path.name" << "\t" << "path.prefix" << "\t" << "path.suffix" << "\t" << "bin" << "\t" << "mean.cov" << "\t" << "mean.inv" << "\t" << "mean.pos" << std::endl;
        algorithms::bin_path_info(graph, (args::get(aggregate_delim) ? args::get(path_delim) : ""),
                                  write_header_tsv,write_tsv, write_seq_noop,
                                  args::get(num_bins), args::get(bin_width));
    }
    return 0;
}

static Subcommand odgi_bin("bin", "bin path information across the graph",
                              PIPELINE, 3, main_bin);


}
