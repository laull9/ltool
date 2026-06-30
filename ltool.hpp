/**
 * @file LTool.hpp
 * @brief ltool 常用组件总入口。
 */

#ifndef LTOOL_INCLUDE
#define LTOOL_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LFmt.hpp"
#include "LString.hpp"
#include "LLog.hpp"
#include "LJson.hpp"

#if LTOOL_HAS_CPP20
#include "LToml.hpp"
#include "LYaml.hpp"
#include "LConfig.hpp"
#endif

#include "LEnv.hpp"
#include "LTimer.hpp"
#include "LRandom.hpp"
#include "LTable.hpp"

#if LTOOL_HAS_FILESYSTEM
#include "LPath.hpp"
#include "LFile.hpp"
#endif

#if LTOOL_HAS_CONCEPTS
#include "Locked.hpp"
#endif

#if LTOOL_HAS_THREAD_POOL
#include "LThreadPool.hpp"
#endif

#endif // LTOOL_INCLUDE
