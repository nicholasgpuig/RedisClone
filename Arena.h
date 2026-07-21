#pragma once

struct Arena {
    char buf[65536];
    char* cursor = buf;
};