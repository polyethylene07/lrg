# lrg
---
一个简易的`grep`实现，仅使用系统调用`open/read/write`读取文件，适用于字面量匹配。可用选项有`-n,-i,-c`。
> 调用形式为`lrg [-nic] PATTERN [FILE...]`

## 构建

```bash
git clone <你的仓库地址>
cd lrg
make
```
> `make`操作会在当前目录`lrg`下生成文件`lrg`，即可通过调用形式进行使用
- `make test`操作则会在`test`目录下生成`data`文件夹，并开始进行测试，若显示“ALL PAST”即为成功
- 若要对测试产生的`data`文件夹进行删除，请使用命令`make clean-data`
- 若要对生成的`lrg`文件和`data`文件夹都进行删除，请使用命令`make clean`

仅包含一个源文件，`src/lrg.c`。

## 用法

```text
lrg [-nic] PATTERN [FILE...]
```
> 在未给文件名的情况下会读取标准输入。

| 选项 | 作用 |
| --- | --- |
| `-n` | 每行前面加行号 |
| `-i` | 只忽略 ASCII 字母的大小写 |
| `-c` | 只输出命中行数，压过 `-n` |


参数会从左往右看，第一个不是选项的会被认为是 PATTERN，剩下都是文件名。
选项可以合并写，`-ni` 表示 `-n -i`。

> 单独一个 `-` 当 PATTERN，不当标准输入——省略文件名已经能读标准输入，多一条特例不值得。

## 示例

在仓库根目录运行一次 `make test`，会生成测试用文件，文件落在 `tests/data/`。以下为提供给测试脚本的输入：

```console
$ ./lrg boot tests/data/sample.log
boot ok
boot ok again

$ ./lrg -ni error tests/data/sample.log
3:ERROR: retry later
5:error: give up

$ ./lrg -ci error tests/data/sample.log tests/data/notes.txt
tests/data/sample.log:2
tests/data/notes.txt:1

$ cat tests/data/sample.log | ./lrg -n boot
1:boot ok
4:boot ok again
```

一个文件打不开，其余照常：

```console
$ ./lrg error tests/data/sample.log tests/data/no-such; echo "exit=$?"
tests/data/sample.log:error: give up
lrg: tests/data/no-such: No such file or directory
exit=2
```

## 测试

```sh
make test
```

> `make clean` 可以把二进制和生成的输入一起删掉。

```console
$ make test
ok   basic
ok   opt-ni
ok   opt-ci
ok   no-match
ok   count-0
ok   bad-opt
ok   no-args
ok   empty-pat
ok   one-bad
ok   stdin-n
ok   big-count
ok   no-trailing-nl
ok   cross-buffer
ok   long-line
---
ALL PASS
```

最后三条是数出来的：12 MiB、20 万行里数出 200000；70000 字节的行输出 70007 字节；1 MiB 单行输出 1048582 字节。

## 实现思路

第一：实现最底层的输出write，让所有的输出都通过他，他可以被信号打断后重试，也能实现短写；emit等的函数就是在他的基础上实现正确的输出格式


第二：输入使用可增长大小的缓冲，并利用缓冲区的指针进行切行，来读一行，并进行匹配

第三：匹配，在这一步完成PATTERN为空时，和识别大写转换为小写去读等等等的功能

第四：这些最后都使用process_fd和main函数总结起来

第五：检查有没有free动态分配的内存，有没有close文件

第六：错误处理按照文件研究怎么report

| 情况 | stderr | 退出码 |
| --- | --- | --- |
| 文件打不开 | `lrg: 文件名: No such file or directory` | 2，其余文件照常处理 |
| 读失败中途出错 | `lrg: 文件名: 错误描述` | 2 |
| 没有 PATTERN | usage 一行 | 2，什么都不处理 |
| 非法选项 | usage 一行 | 2 |

参数错误在打开任何文件之前就退应当退出。

## 已知限制

- 不支持正则，pattern一律按字面量处理。
- 不支持`-r`、`-v`、`-E`、`--`。
- `-i` 只折 ASCII，非ASCII字节会按原样比较。
- 所有输入都当作文本处理，不会把NUL字节特殊对待。grep则会正常检测二进制数据
- 不把`-`当作"标准输入"的简写，只是当作普通参数处理