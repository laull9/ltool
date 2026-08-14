/**
 * @file LDialog.hpp
 * @brief LTool 的原生文件、目录、消息和通知对话框入口。
 *
 * LDialog 基于 vendored portable-file-dialogs，保持纯头文件形态。
 * 主要接口：
 * - message()  ：弹出提示、警告、错误等模态消息框。
 * - notify()   ：系统通知气泡。
 * - open_file()/save_file()  ：文件选择框。
 * - open_folder()             ：目录选择框。
 *
 * 常用示例：
 * @code
 * LDialog::message("提示", "文件已保存");
 * auto path = LDialog::open_file("选择文件", ".");
 * if (path) { LLOG_INFO("selected {}", *path); }
 * @endcode
 */

#ifndef LTOOL_LDIALOG_INCLUDE
/// LDialog.hpp 的头文件包含保护宏。
#define LTOOL_LDIALOG_INCLUDE

#include "detail/LDialog.hpp"

#endif // LTOOL_LDIALOG_INCLUDE
