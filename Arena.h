#pragma once

struct Arena {
    char buf[8192];
    char* cursor = buf;
};