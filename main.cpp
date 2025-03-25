#include <iostream>
#include <vector>
#include <memory>
#include <random>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>

class GameState {
public:
    std::vector<int> sequence;
    int player1_score;
    int player2_score;
    bool is_player1_turn;

    GameState(const std::vector<int>& seq = {}, int p1 = 0, int p2 = 0, bool turn = true)
        : sequence(seq), player1_score(p1), player2_score(p2), is_player1_turn(turn) {}

    bool isTerminal() const {
        return sequence.size() == 1;
    }

    int getWinner() const {
        if (!isTerminal()) return 0;
        if (player1_score > player2_score) return 1;
        if (player2_score > player1_score) return 2;
        return 0;
    }

    bool operator==(const GameState& other) const {
        return sequence == other.sequence &&
               player1_score == other.player1_score &&
               player2_score == other.player2_score &&
               is_player1_turn == other.is_player1_turn;
    }

    std::vector<GameState> generateNextStates() const {
        std::vector<GameState> next_states;
        next_states.reserve(sequence.size() - 1);

        for (size_t i = 0; i < sequence.size() - 1; ++i) {
            int sum = sequence[i] + sequence[i+1];
            std::vector<int> new_sequence = sequence;
            new_sequence.erase(new_sequence.begin() + i, new_sequence.begin() + i + 2);

            int new_p1 = player1_score;
            int new_p2 = player2_score;

            if (sum > 7) {
                new_sequence.insert(new_sequence.begin() + i, 1);
                if (is_player1_turn) new_p1 += 2;
                else new_p2 += 2;
            }
            else if (sum < 7) {
                new_sequence.insert(new_sequence.begin() + i, 3);
                if (is_player1_turn) new_p2 -= 1;
                else new_p1 -= 1;
            }
            else {
                new_sequence.insert(new_sequence.begin() + i, 2);
                if (is_player1_turn) new_p1 -= 1;
                else new_p2 -= 1;
            }

            next_states.emplace_back(new_sequence, new_p1, new_p2, !is_player1_turn);
        }

        return next_states;
    }

    std::string toJsonString() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"sequence\":[";
        for (size_t i = 0; i < sequence.size(); ++i) {
            if (i != 0) oss << ",";
            oss << sequence[i];
        }
        oss << "],";
        oss << "\"player1_score\":" << player1_score << ",";
        oss << "\"player2_score\":" << player2_score << ",";
        oss << "\"is_player1_turn\":" << (is_player1_turn ? "true" : "false") << ",";
        oss << "\"is_terminal\":" << (isTerminal() ? "true" : "false") << ",";
        oss << "\"winner\":" << getWinner();
        oss << "}";
        return oss.str();
    }
};

namespace std {
    template<>
    struct hash<GameState> {
        size_t operator()(const GameState& state) const {
            size_t h = 0;
            for (int num : state.sequence) {
                h ^= hash<int>()(num) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            h ^= hash<int>()(state.player1_score);
            h ^= hash<int>()(state.player2_score);
            h ^= hash<bool>()(state.is_player1_turn);
            return h;
        }
    };
}

struct GameTreeNode {
    GameState state;
    std::vector<std::unique_ptr<GameTreeNode>> children;
    int id;

    GameTreeNode(const GameState& game_state = GameState(), int node_id = 0)
        : state(game_state), id(node_id) {}

    std::string toJsonString(int indent = 0) const {
        std::string indent_str(indent, ' ');
        std::ostringstream oss;

        oss << indent_str << "{\n";
        oss << indent_str << "  \"id\": " << id << ",\n";
        oss << indent_str << "  \"state\": " << state.toJsonString() << ",\n";
        oss << indent_str << "  \"children\": [\n";

        for (size_t i = 0; i < children.size(); ++i) {
            if (i != 0) oss << ",\n";
            oss << children[i]->toJsonString(indent + 4);
        }

        if (!children.empty()) oss << "\n";
        oss << indent_str << "  ]\n";
        oss << indent_str << "}";

        return oss.str();
    }
};

struct WorkItem {
    GameTreeNode* parent_node;
    GameState current_state;

    WorkItem() : parent_node(nullptr), current_state() {}  // Default constructor
    WorkItem(GameTreeNode* parent, const GameState& state)
        : parent_node(parent), current_state(state) {}
};

class ConcurrentQueue {
private:
    std::queue<WorkItem> queue;
    mutable std::mutex mutex;
    std::condition_variable cv;
public:
    void push(WorkItem value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(value));
        cv.notify_one();
    }

    bool try_pop(WorkItem& value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        value = std::move(queue.front());
        queue.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
};

std::atomic<int> global_node_id{1};
std::unordered_map<GameState, int> global_state_cache;
std::mutex cache_mutex;
std::mutex tree_mutex;
ConcurrentQueue work_queue;
std::atomic<bool> stop_flag{false};
std::atomic<int> active_workers{0};

void worker_thread() {
    active_workers++;
    while (!stop_flag || !work_queue.empty()) {
        WorkItem task;
        if (work_queue.try_pop(task)) {
            if (task.parent_node == nullptr) continue;

            const GameState& current_state = task.current_state;
            if (current_state.isTerminal()) {
                continue;
            }

            // Check cache
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                auto it = global_state_cache.find(current_state);
                if (it != global_state_cache.end()) {
                    continue;
                }
                global_state_cache[current_state] = task.parent_node->id;
            }

            // Generate next states
            auto next_states = current_state.generateNextStates();

            // Create child nodes
            std::vector<std::unique_ptr<GameTreeNode>> children;
            children.reserve(next_states.size());

            for (const auto& next_state : next_states) {
                int new_id = ++global_node_id;
                children.emplace_back(std::make_unique<GameTreeNode>(next_state, new_id));
                work_queue.push(WorkItem(children.back().get(), next_state));
            }

            // Add children to parent node
            {
                std::lock_guard<std::mutex> lock(tree_mutex);
                task.parent_node->children = std::move(children);
            }
        } else {
            std::this_thread::yield();
        }
    }
    active_workers--;
}

void buildTreeMultithreaded(GameTreeNode* root) {
    unsigned num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " threads for tree construction\n";

    work_queue.push(WorkItem(root, root->state));

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker_thread);
    }

    // Wait for all work to complete
    while (!work_queue.empty() || active_workers > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop_flag = true;

    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

std::vector<int> generateRandomSequence(int length = 14) {
    std::vector<int> sequence(length);
    std::mt19937 generator(std::time(nullptr));
    std::uniform_int_distribution<int> distribution(1, 9);

    for (int& num : sequence) {
        num = distribution(generator);
    }

    return sequence;
}

int main() {
    auto total_start = std::chrono::high_resolution_clock::now();

    auto gen_start = std::chrono::high_resolution_clock::now();
    std::vector<int> initial_sequence = generateRandomSequence(14);
    auto gen_end = std::chrono::high_resolution_clock::now();

    std::cout << "Initial sequence (" << initial_sequence.size() << " numbers): ";
    for (int num : initial_sequence) {
        std::cout << num << " ";
    }
    std::cout << "\n\n";

    GameState initial_state(initial_sequence);
    GameTreeNode root_node(initial_state, 1);
    global_node_id = 1;

    std::cout << "Building game tree...\n";
    auto build_start = std::chrono::high_resolution_clock::now();
    buildTreeMultithreaded(&root_node);
    auto build_end = std::chrono::high_resolution_clock::now();

    std::cout << "Saving to JSON...\n";
    auto save_start = std::chrono::high_resolution_clock::now();
    std::ofstream output_file("game_tree.json");
    output_file << root_node.toJsonString();
    output_file.close();
    auto save_end = std::chrono::high_resolution_clock::now();
    auto total_end = std::chrono::high_resolution_clock::now();

    auto gen_duration = std::chrono::duration_cast<std::chrono::milliseconds>(gen_end - gen_start);
    auto build_duration = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start);
    auto save_duration = std::chrono::duration_cast<std::chrono::milliseconds>(save_end - save_start);
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);

    std::cout << "\nPerformance Metrics:\n";
    std::cout << "--------------------------------\n";
    std::cout << "Sequence generation time: " << gen_duration.count() << " ms\n";
    std::cout << "Tree construction time:   " << build_duration.count() << " ms\n";
    std::cout << "JSON saving time:        " << save_duration.count() << " ms\n";
    std::cout << "--------------------------------\n";
    std::cout << "Total execution time:    " << total_duration.count() << " ms\n";
    std::cout << "--------------------------------\n";
    std::cout << "Done! Tree saved to game_tree.json\n";
    std::cout << "Total nodes created: " << global_node_id << "\n";
    std::cout << "Unique states: " << global_state_cache.size() << "\n";

    return 0;
}