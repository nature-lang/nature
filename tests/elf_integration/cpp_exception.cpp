#include <string>
#include <vector>

struct global_state {
    global_state() : value(7) {}
    ~global_state() { value = 0; }
    int value;
};

static global_state state;

static int exercise_cpp_runtime() {
    std::vector<int> values{1, 2, 3, 4};
    std::string text = "nature-elf";
    int sum = 0;
    for (int value: values)
        sum += value;
    if (sum != 10 || text.size() != 10 || state.value != 7)
        return -1;
    throw sum + state.value;
}

int main() {
    try {
        (void) exercise_cpp_runtime();
        return 1;
    } catch (int result) {
        return result == 17 ? 0 : 2;
    } catch (...) {
        return 2;
    }
}
