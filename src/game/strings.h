// Case-insensitive string comparison (used to sort and search file and track names).
#pragma once
#include "bridge/msvc.h"

bool differs_nocase(const MsString *a, const MsString *b);
bool less_nocase(const MsString *a, const MsString *b);
