#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include "Storage.h"
#include "Router.h"
#include "handle.h"

Router::Router(Storage& storage) : storage_(storage) {}

void Router::add(std::string name, Handler handler) {
    std::ranges::transform(name, name.begin(), ::toupper);
    m[name] = handler;
}

std::string Router::dispatch(std::string_view name, const std::vector<std::string>& args) {
    std::string upper_name(name);
    std::ranges::transform(upper_name, upper_name.begin(), ::toupper);
    auto it = m.find(upper_name);
    if (it != m.end()) {
        return it->second(args, storage_);
    }
    return handle_unknown();
}

//      One thing to flag for later, not now: erase()-from-front on a std::string-as-buffer is O(n) per command (shifts all remaining bytes). Fine for Phase 1 correctness; when you get to performance tuning you'll likely switch to an offset-tracking or ring buffer so you're not shifting memory on every command.