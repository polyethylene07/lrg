

#define _POSIX_C_SOURCE 200809L


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      //系统调用的用户态接口，如STDOUT_FILENO本身是一个常数1，就在这个库里，是输出到
#include <fcntl.h>       //读fd，作为参数，可以指定一个位置，如标准输入，标准输出，标准错误
#include <errno.h>       //带了一个宏errono，和一个函数strerror以后用他来report错误

#define INIT_CAP        (64 * 1024)   // 初始缓冲区大小64kb 
#define STDIN_NAME      "(standard input)"
#define SIZE_MAX        ((size_t)-1)  

/* 这个函数可以把指定东西写入到指定地方，write可能被信号打断，所以写循环。被emit等函数使用，最底层的写入函数 */
static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);  //返回写入的数字数量或-1
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)                     //要写入的长度不为0，却未写入，则报错
            return -1;
        p += (size_t)n;                 //缓冲区移动到下一个地方，长度减小
        len -= (size_t)n;
    }
    return 0;
}
/* 这个是被emit_line使用的，负责行内容之前的前缀部分，文件名，和这是第几行 */
static void emit_prefix(const char *name, int with_name,
                        int show_num, unsigned long long lineno)
{
    char num[32];

    if (with_name) {
        //只是输出一个名字
        write_all(STDOUT_FILENO, name, strlen(name));
        write_all(STDOUT_FILENO, ":", 1);
    }
    if (show_num) {
        //只是输出行号
        int k = snprintf(num, sizeof num, "%llu:", lineno); 
        /* ai：snprintf保证：最多写入到缓冲区 size 个字节（包括结尾的 \0）。超出的部分直接丢弃，绝不越界。所以它不会造成缓冲区溢出。
        * snprintf返回的值是他的实际大小
        */
        write_all(STDOUT_FILENO, num, (size_t)k); //把缓冲区的东西输出出去
    }
}

/* 输出一个匹配行；行内容原样输出，末尾一定补且只补一个 \n。注意：他只是针对于一行的，这样的话是用于确保一行的格式正确 */
static void emit_line(const char *name, int with_name, int show_num,
                      unsigned long long lineno, const char *line, size_t len)
{
    emit_prefix(name, with_name, show_num, lineno);
    if (len > 0)
        write_all(STDOUT_FILENO, line, len);
    write_all(STDOUT_FILENO, "\n", 1);
}

/* -c：只输出数量。name 为 NULL 表示单输入（不加前缀）。 */
static void emit_count(const char *name, unsigned long long count)
{
    char num[32];
    int k = snprintf(num, sizeof num, "%llu", count);

    if (name != NULL) {
        write_all(STDOUT_FILENO, name, strlen(name));
        write_all(STDOUT_FILENO, ":", 1);
    }
    write_all(STDOUT_FILENO, num, (size_t)k);
    write_all(STDOUT_FILENO, "\n", 1);
}

/* -------------------------------------------------------------- 报错 --- */

static void report_io_error(const char *name, int err)
{//自带一个err的int数转为string描述的函数
    fprintf(stderr, "lrg: %s: %s\n", name, strerror(err));     //fprintf 可以输出到任意文件流。
}

static void usage(void)
{//只是一个提示用法的，我自己写就可以
    fprintf(stderr, "usage: lrg [-nic] PATTERN [FILE...]\n");
    exit(2);
}

/* -------------------------------------------------------------- 匹配 --- */
//从网上直接拿到的函数
static char ascii_fold(unsigned char c)
{//把大写范围内的都减去大写的值再加上小写
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : (char)c;
}

/*
 * 在 line[0..len) 里找 pat[0..plen)。
 * 空 pattern 匹配一切（题目要求）。
 */
static int line_matches(const char *line, size_t len,
                        const char *pat, size_t plen, int icase)
{
    size_t i, j;

    if (plen == 0)
        return 1;
    if (plen > len)
        return 0;

    if (!icase) {
        /* 先用 memchr 找首字节，命中再用 memcmp 核对，比逐字节循环快。 */
        i = 0;
        while (i + plen <= len) {
            const char *p = memchr(line + i, pat[0], len - plen + 1 - i);
            if (p == NULL)
                return 0;
            if (memcmp(p, pat, plen) == 0)
                return 1;
            i = (size_t)(p - line) + 1;
        }
        return 0;
    }

    for (i = 0; i + plen <= len; i++) {
        for (j = 0; j < plen; j++) {
            if (ascii_fold((unsigned char)line[i + j]) !=
                ascii_fold((unsigned char)pat[j]))
                break;
        }
        if (j == plen)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------- 输入流的处理 --- */

/*
 * 把一个 fd 从头读到尾，边读边切行。统一为一整个进程
 * 
 * 如果匹配上了，就会输出
 * 返回值：0 表示正常读完（不管有没有匹配），-1 表示读失败。
 * 
 * 根据程算课所讲，想返回多个参数，应当使用指针，所以用*nmatched 输出匹配行数，*failed 置 1 表示发生了 I/O 错误。
 */
static int process_fd(int fd, const char *name,
                      const char *pat, size_t plen, int icase,
                      int show_num, int count_only, int with_name,
                      unsigned long long *nmatched, int *failed)
{   //设置一个读行的区域，用偏移量不用指针
    size_t cap = INIT_CAP;     //cap还是初始缓冲区大小
    size_t start = 0;          // 偏移量，当前未处理数据的起点（buf 内偏移） 
    size_t end = 0;            // 偏移量，已读入数据的终点 
    //给地方放这个区域
    char *buf = malloc(cap);   //这个的地址真的是指针
    //初始化计数器
    unsigned long long lineno = 0;   //行号
    unsigned long long count = 0;    //计数
    int eof = 0;                     //eof情况

    *nmatched = 0;
    //缓冲区不能是有问题的
    if (buf == NULL) {
        report_io_error(name, ENOMEM);
        *failed = 1;
        return -1;
    }

    for (;;) {
        //memchr在一块原始内存中查找某个字节第一次出现的位置，第一个参数是从什么适合开始找，第二个参数是找什么字节，第三个参数表示最多找几个
        char *nl = (start < end) ? memchr(buf + start, '\n', end - start) : NULL;
        /*如果找到这一行的行号了，那就标记从这里到那里为一段，这将记录一个行号，并开始进行匹配，NULL 表示当前缓冲里还没有完整的一行
        * 如果是只读就不输出出去了
        */
        if (nl != NULL) {
            size_t linelen = (size_t)(nl - (buf + start));

            lineno++;
            if (line_matches(buf + start, linelen, pat, plen, icase)) {
                count++;
                if (!count_only)
                    emit_line(name, with_name, show_num, lineno,
                              buf + start, linelen);
            }
            //这一行的生命周期结束了
            start += linelen + 1;
            continue;
        }
        //记eof为1时才运行
        if (eof) {
            /* 文件最后一行可能没有 \n，它仍然是一行。 */
            if (start < end) {
                lineno++;
                if (line_matches(buf + start, end - start, pat, plen, icase)) {
                    count++;
                    if (!count_only)
                        emit_line(name, with_name, show_num, lineno,
                                  buf + start, end - start);
                }
            }
            break;
        }

        /* 把没处理完的尾巴挪到开头，腾出空间来读新数据。 */
        if (start > 0) {
            //这一步以后再想有没有更好的搬数据方法吧
            memmove(buf, buf + start, end - start);
            end -= start;
            start = 0;
        }

        /* 缓冲塞满了还没见到 \n：说明这一行比当前缓冲还长，翻倍。 */
        if (end == cap) {
            char *nb;

            if (cap > SIZE_MAX / 2) {
                report_io_error(name, ENOMEM);
                *failed = 1;
                free(buf);
                return -1;
            }
            cap *= 2;
            nb = realloc(buf, cap);
            if (nb == NULL) {
                report_io_error(name, ENOMEM);
                *failed = 1;
                free(buf);
                return -1;
            }
            buf = nb;
        }

        {
            ssize_t n = read(fd, buf + end, cap - end);

            if (n < 0) {
                if (errno == EINTR)
                    continue;               /* 被信号打断，重读 */
                report_io_error(name, errno);
                *failed = 1;
                free(buf);
                return -1;
            }
            if (n == 0)
                eof = 1;
            else
                end += (size_t)n;
        }
    }

    //c选项下的最终输出接口
    if (count_only)
        emit_count(with_name ? name : NULL, count);

    *nmatched = count;
    free(buf);
    return 0;
}

/* --------------------------------------------------------------- main --- */
//把能拿到的东西简化为这两个，argc：一个整数，表示命令行上总共有几个参数，包括程序名本身。argv就是这几个参数的字符串指针的指针列表
int main(int argc, char **argv){
    //三种选项
    int opt_n = 0, opt_i = 0, opt_c = 0;
    //pattern
    const char *pat;
    //需要匹配的字符串的大小
    size_t plen;
    //文件有多少和 nfiles > 1 的结果，给了几个文件名，会决定要不要加文件名前缀
    int i, nfiles, with_name;
    int failed = 0, matched = 0;

    /* 选项解析：遇到第一个不以 - 开头（或单独的 -）就停。 */
    i = 1;
    while (i < argc) {
        //解引用拿到第i个字符串的首地址
        const char *a = argv[i];
        const char *p;          //为了不影响字符串数组a本身，我用一个p来承接，相当于rbp吧
        //空着的不算数，
        if (a[0] != '-' || a[1] == '\0')
            break;
        for (p = a + 1; *p != '\0'; p++) {
            switch (*p) {
            case 'n': opt_n = 1; break;
            case 'i': opt_i = 1; break;
            case 'c': opt_c = 1; break;
            default:  usage();           /* -x、-nix 都算非法 */
            }
        }
        i++;
    }

    if (i >= argc)                       /* 没有 PATTERN */
        usage();

    pat = argv[i++];
    plen = strlen(pat);
    nfiles = argc - i;
    with_name = (nfiles > 1);


    if (nfiles == 0) {
        unsigned long long cnt = 0;  //用于返回匹配行数的
        int f = 0;  //用于返回是否失败的
        //由于根本没有文件，所以放入的是标准输入
        process_fd(STDIN_FILENO, STDIN_NAME, pat, plen, opt_i,
                   opt_n && !opt_c, opt_c, 0, &cnt, &f);
        if (f)
            failed = 1;
        if (cnt > 0)
            matched = 1;
    } 
    else {
        int k;

        for (k = 0; k < nfiles; k++) {
            //主要是为了拿到文件名，文件名可以open read文件
            const char *name = argv[i + k];
            unsigned long long cnt = 0;
            int f = 0;
            int fd;

            do {
                // open返回的东西是文件的标识符
                fd = open(name, O_RDONLY);
            } while (fd < 0 && errno == EINTR);   
            //这个循环是ai让写的，要在每一个读取文件啊之类的操作放上一个这个循环，防止突如其来的打断

            if (fd < 0) {
                report_io_error(name, errno);
                failed = 1;              /* 打不开也要继续处理后面的文件 */
                continue;
            }

            process_fd(fd, name, pat, plen, opt_i, opt_n && !opt_c,
                       opt_c, with_name, &cnt, &f);
            close(fd);

            if (f)
                failed = 1;
            if (cnt > 0)
                matched = 1;
        }
    }

    if (failed)
        return 2;
    return matched ? 0 : 1;    //终于写完了，其实感觉这个也不错(failed)?2:((matched)?0:1)
}
