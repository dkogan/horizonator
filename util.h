#pragma once

#include <stdio.h>
#define MSG(fmt, ...) fprintf(stderr, "%s(%d) at %s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
