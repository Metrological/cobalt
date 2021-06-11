// Copyright 2018 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Specialized defines for porting V8 on top of Starboard.  It would have been
// nice to have been able to use the shared poems, however they are too
// aggressive for V8 (such as grabbing identifiers that will come after std::).

#ifndef V8_SRC_POEMS_H_
#define V8_SRC_POEMS_H_

#if !defined(V8_OS_STARBOARD)
#error "Including V8 poems without V8_OS_STARBOARD defined."
#endif

#include "starboard/memory.h"
#include "starboard/string.h"
#include "starboard/common/log.h"

#define malloc(x) SbMemoryAllocate(x)
#define realloc(x, y) SbMemoryReallocate(x, y)
#define free(x) SbMemoryDeallocate(x)
#define calloc(x, y) SbMemoryCalloc(x, y)
#define strdup(s) SbStringDuplicate(s)
#define printf(x) SbLogRaw(x)
#define __builtin_abort SbSystemBreakIntoDebugger

// No-ops for now
#define fopen(x)
#define fclose(x)
#define feof(x) 1
#define fgets(x, y, z) nullptr
#define ferror(x) 1
#define fseek(x, y, z) 1
#define fread(x, y, z, q) 0
#define ftell(x) -1L
#define puts(x)
#define fputs(x)
#define fprintf(x, y, z)

static FILE* null_file_ptr = nullptr;
#undef stderr
#define stderr null_file_ptr
#undef stdout
#define stdout null_file_ptr
#define fflush(x)

#endif  // V8_SRC_POEMS_H_
