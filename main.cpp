#include <iostream>
#include <vector>
#include <memory>
#include <random>
#include <ctime>
#include <fstream>
#include <unordered_map>
#include <chrono>

class GameState {
public:
    std::vector<int> sequence;
    int p1_score;
    int p2_score;
    bool is_p1_turn;

    GameState(const std::vector<int>& seq = {}, int p1 = 0, int p2 = 0, bool turn = true)
        : sequence(seq), p1_score(p1), p2_score(p2), is_p1_turn(turn) {}

    bool isTerminal() const {
        return sequence.size() == 1;
    }

    int getWinner() const {
        if (!isTerminal()) return 0;
        if (p1_score > p2_score) return 1;
        if (p2_score > p1_score) return 2;
        return 0;
    }

    bool operator==(const GameState& other) const {
        return sequence == other.sequence &&
               p1_score == other.p1_score &&
               p2_score == other.p2_score &&
               is_p1_turn == other.is_p1_turn;
    }

    std::vector<GameState> generateNextStates() const {
        std::vector<GameState> next_states;
        next_states.reserve(sequence.size() - 1);

        for (size_t i = 0; i < sequence.size() - 1; ++i) {
            int sum = sequence[i] + sequence[i+1];
            std::vector<int> new_sequence = sequence;
            new_sequence.erase(new_sequence.begin() + i, new_sequence.begin() + i + 2);

            int new_p1 = p1_score;
            int new_p2 = p2_score;

            if (sum > 7) {
                new_sequence.insert(new_sequence.begin() + i, 1);
                if (is_p1_turn) new_p1 += 2;
                else new_p2 += 2;
            }
            else if (sum < 7) {
                new_sequence.insert(new_sequence.begin() + i, 3);
                if (is_p1_turn) new_p2 -= 1;
                else new_p1 -= 1;
            }
            else {
                new_sequence.insert(new_sequence.begin() + i, 2);
                if (is_p1_turn) new_p1 -= 1;
                else new_p2 -= 1;
            }

            next_states.emplace_back(new_sequence, new_p1, new_p2, !is_p1_turn);
        }

        return next_states;
    }

    void writeJsonTo(std::ostream& os) const {
        os << "{";
        os << "\"sequence\":[";
        if (!sequence.empty()) {
            os << sequence[0];
            for (size_t i = 1; i < sequence.size(); ++i) {
                os << "," << sequence[i];
            }
        }
        os << "],";
        os << "\"player1_score\":" << p1_score << ",";
        os << "\"player2_score\":" << p2_score << ",";
        os << "\"is_player1_turn\":" << (is_p1_turn ? "true" : "false") << ",";
        os << "\"is_terminal\":" << (isTerminal() ? "true" : "false") << ",";
        os << "\"winner\":" << getWinner();
        os << "}";
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
            h ^= hash<int>()(state.p1_score);
            h ^= hash<int>()(state.p2_score);
            h ^= hash<bool>()(state.is_p1_turn);
            return h;
        }
    };
}

struct GameTreeNode {
    GameState state;
    std::vector<std::unique_ptr<GameTreeNode>> children;
    int id;
    int depth;

    GameTreeNode(const GameState& game_state = GameState(), int node_id = 0, int d = 0)
        : state(game_state), id(node_id), depth(d) {}

    void writeJsonTo(std::ostream& os, int indent = 0) const {
        std::string indent_str(indent, ' ');

        os << indent_str << "{\n";
        os << indent_str << "  \"id\": " << id << ",\n";
        os << indent_str << "  \"depth\": " << depth << ",\n";
        os << indent_str << "  \"state\": ";
        state.writeJsonTo(os);
        os << ",\n";
        os << indent_str << "  \"children\": [";

        if (!children.empty()) {
            os << "\n";
            for (size_t i = 0; i < children.size(); ++i) {
                if (i != 0) os << ",\n";
                children[i]->writeJsonTo(os, indent + 4);
            }
            os << "\n" << indent_str << "  ";
        }

        os << "]\n";
        os << indent_str << "}";
    }
};

void buildTree(GameTreeNode* node, int current_depth, int max_depth,
               std::unordered_map<GameState, int>& state_cache, int& node_counter) {
    if (node->state.isTerminal() || current_depth >= max_depth) {
        return;
    }

    if (state_cache.count(node->state)) {
        return;
    }
    state_cache[node->state] = node->id;

    auto next_states = node->state.generateNextStates();
    for (const auto& next_state : next_states) {
        int new_id = ++node_counter;
        auto new_node = std::make_unique<GameTreeNode>(next_state, new_id, current_depth + 1);
        buildTree(new_node.get(), current_depth + 1, max_depth, state_cache, node_counter);
        node->children.push_back(std::move(new_node));
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
    int depth_limit = 8; // PAMAINI SO UZ ZEMAKU/AUGSTAKU VERTIBU, BET 8 LIMENIEM VAJADZETU BUT +- OK

    auto total_start = std::chrono::high_resolution_clock::now();

    auto gen_start = std::chrono::high_resolution_clock::now();
    std::vector<int> initial_sequence = generateRandomSequence(20);
    auto gen_end = std::chrono::high_resolution_clock::now();

    std::cout << "Initial sequence (" << initial_sequence.size() << " numbers): ";
    for (int num : initial_sequence) {
        std::cout << num << " ";
    }
    std::cout << "\n\n";

    GameState initial_state(initial_sequence);
    auto root_node = std::make_unique<GameTreeNode>(initial_state, 1, 0);
    int node_counter = 1;
    std::unordered_map<GameState, int> state_cache;

    std::cout << "Building game tree...\n";
    auto build_start = std::chrono::high_resolution_clock::now();
    buildTree(root_node.get(), 0, depth_limit, state_cache, node_counter);
    auto build_end = std::chrono::high_resolution_clock::now();

    std::cout << "Saving to JSON...\n";
    auto save_start = std::chrono::high_resolution_clock::now();
    std::ofstream output_file("game_tree.json", std::ios::binary);
    root_node->writeJsonTo(output_file);
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
    std::cout << "JSON saving time:         " << save_duration.count() << " ms\n";
    std::cout << "--------------------------------\n";
    std::cout << "Total execution time:     " << total_duration.count() << " ms\n";
    std::cout << "--------------------------------\n";
    std::cout << "Done! Tree saved to game_tree.json\n";
    std::cout << "Total nodes created: " << node_counter << "\n";
    std::cout << "Unique states: " << state_cache.size() << "\n";
    std::cout << "Depth limit: " << depth_limit << "\n";

    return 0;
}