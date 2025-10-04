#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <vector>
#include <curl/curl.h>
#include <stdexcept>
#include "rapidjson/error/error.h"
#include "rapidjson/reader.h"
#include <chrono>
#include "rapidjson/document.h"
#include <thread>              
#include <mutex>               
#include <condition_variable>  
#include <atomic>              


struct ParseException : std::runtime_error, rapidjson::ParseResult {
    ParseException(rapidjson::ParseErrorCode code, const char* msg, size_t offset) : 
        std::runtime_error(msg), 
        rapidjson::ParseResult(code, offset) {}
};

#define RAPIDJSON_PARSE_ERROR_NORETURN(code, offset) \
    throw ParseException(code, #code, offset)

#include <rapidjson/document.h>
#include <chrono>


bool debug = false;

// Updated service URL
const std::string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";

// Function to HTTP ecnode parts of URLs. for instance, replace spaces with '%20' for URLs
std::string url_encode(CURL* curl, std::string input) {
  char* out = curl_easy_escape(curl, input.c_str(), input.size());
  std::string s = out;
  curl_free(out);
  return s;
}

// Callback function for writing response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// Function to fetch neighbors using libcurl with debugging
std::string fetch_neighbors(CURL* curl, const std::string& node) {

  std::string url = SERVICE_URL + url_encode(curl, node);
  std::string response;

    if (debug)
      std::cout << "Sending request to: " << url << std::endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose Logging

    // Set a User-Agent header to avoid potential blocking by the server
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
    } else {
      if (debug)
        std::cout << "CURL request successful!" << std::endl;
    }

    // Cleanup
    curl_slist_free_all(headers);

    if (debug) 
      std::cout << "Response received: " << response << std::endl;  // Debug log

    return (res == CURLE_OK) ? response : "{}";
}

// Function to parse JSON and extract neighbors
std::vector<std::string> get_neighbors(const std::string& json_str) {
    std::vector<std::string> neighbors;
    try {
      rapidjson::Document doc;
      doc.Parse(json_str.c_str());
      
      if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray())
	  neighbors.push_back(neighbor.GetString());
      }
    } catch (const ParseException& e) {
      std::cerr<<"Error while parsing JSON: "<<json_str<<std::endl;
      throw e;
    }
    return neighbors;
}

// BlockingQueue class
template<typename T>
class BlockingQueue {
private:
    std::queue<T> q;
    std::mutex m;
    std::condition_variable cv;
public:
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lk(m);
            q.push(item);
        }
        cv.notify_one();
    }

    bool pop(T& out, std::atomic<bool>& done) {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&]{ return !q.empty() || done.load(); });
        if (q.empty()) return false;
        out = q.front();
        q.pop();
        return true;
    }

    void notify_all() { cv.notify_all(); }
};

struct WorkItem {
    std::string node;
    int level;
};

// Parallel BFS implementation using BlockingQueue
std::vector<std::vector<std::string>> parallel_bfs(CURL* /*unused*/, const std::string& start, int depth) {
    const int num_threads = 8;
    BlockingQueue<WorkItem> queue;
    std::unordered_set<std::string> visited;
    std::mutex visited_m;
    std::mutex levels_m;
    std::atomic<int> tasks(0);
    std::atomic<bool> done(false);
    std::vector<std::vector<std::string>> levels(depth + 1);

    {
        std::lock_guard<std::mutex> lk(visited_m);
        visited.insert(start);
    }
    queue.push(WorkItem{start, 0});
    tasks.fetch_add(1);

    auto worker = [&]() {
        CURL* curl = curl_easy_init();
        if (!curl) return;

        while (true) {
            WorkItem item;
            if (!queue.pop(item, done)) {
                if (done.load()) break;
                else continue;
            }

            if (item.level <= depth) {
                std::lock_guard<std::mutex> lk(levels_m);
                levels[item.level].push_back(item.node);
            }

            if (item.level < depth) {
                try {
                    std::string json = fetch_neighbors(curl, item.node);
                    auto neigh = get_neighbors(json);
                    for (const auto& n : neigh) {
                        bool should_enqueue = false;
                        {
                            std::lock_guard<std::mutex> lk(visited_m);
                            if (!visited.count(n)) {
                                visited.insert(n);
                                should_enqueue = true;
                            }
                        }
                        if (should_enqueue) {
                            queue.push(WorkItem{n, item.level + 1});
                            tasks.fetch_add(1);
                        }
                    }
                } catch (const ParseException&) {
                    // keep this (part of original framework)
                }
            }

            int remaining = tasks.fetch_sub(1) - 1;
            if (remaining == 0) {
                done.store(true);
                queue.notify_all();
            }
        }

        curl_easy_cleanup(curl);
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; ++i) workers.emplace_back(worker);
    for (auto &t : workers) if (t.joinable()) t.join();

    return levels;
}

// Sequential BFS
std::vector<std::vector<std::string>> bfs(CURL* curl, const std::string& start, int depth) {
    std::vector<std::vector<std::string>> levels;
    std::unordered_set<std::string> visited;
    levels.push_back({start});
    visited.insert(start);

    for (int d = 0; d < depth; d++) {
        levels.push_back({});
        for (std::string& s : levels[d]) {
            try {
                for (const auto& neighbor : get_neighbors(fetch_neighbors(curl, s))) {
                    if (!visited.count(neighbor)) {
                        visited.insert(neighbor);
                        levels[d+1].push_back(neighbor);
                    }
                }
            } catch (const ParseException&) {
                // keep (given in starter)
            }
        }
    }
    return levels;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " [--sequential|--parallel] <node_name> <depth>\n";
        return 1;
    }

    bool use_parallel = true;
    std::string start_node;
    int depth;

    if (argc == 4) {
        std::string mode = argv[1];
        if (mode == "--sequential")
            use_parallel = false;
        else if (mode != "--parallel") {
            std::cerr << "Error: Unknown mode '" << mode << "'. Use --sequential or --parallel.\n";
            return 1;
        }
        start_node = argv[2];
        depth = std::stoi(argv[3]);
    } else {
        start_node = argv[1];
        depth = std::stoi(argv[2]);
    }

    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl = curl_easy_init();

    const auto start = std::chrono::steady_clock::now();

    auto results = use_parallel
        ? parallel_bfs(curl, start_node, depth)
        : bfs(curl, start_node, depth);

    for (const auto& n : results) {
        for (const auto& node : n)
            std::cout << "- " << node << "\n";
        std::cout << n.size() << "\n";
    }

    const auto finish = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed_seconds{finish - start};
    std::cout << "Time to crawl (" << (use_parallel ? "parallel" : "sequential")
              << "): " << elapsed_seconds.count() << "s\n";

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}