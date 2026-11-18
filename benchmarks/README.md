# 性能基准

在仓库根目录运行。内存使用 glibc 2.33+ 的 mallinfo2，记录进程分配器的存活字节数。遍历按 16 次调用的平均微秒计时。头文件脚本在 Linux 上记录三次独立编译的耗时中位数和最大 RSS。

```sh
c++ -std=c++20 -O2 -I. benchmarks/json.cpp -o /tmp/ltool-json-bench
/tmp/ltool-json-bench
CXX=g++ python3 benchmarks/headers.py
```
