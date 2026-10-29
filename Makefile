CC      ?= cc							# 如果没有指定c编译器，就默认使用系统自带的编译器
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra # -std=gnu11 使用 GNU C11 标准编译，即在 C11 基础上允许 GNU 扩展。开启二级优化，常用的平衡优化级别。开启大部分常用编译警告。开启 -Wall 之外的额外警告。
TARGET   = lrg                          # 不用后缀，这是个名字，由编译命令决定最终变成啥
SRC      = src/lrg.c


all: $(TARGET)							# 第一个出现的为默认选项，这个操作是确保target已经构建好了

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)		
# cc -std=gnu11 -O2 -Wall -Wextra -o lrg src/lrg.c的意思 $@是目标名的可执行文件名

test: $(TARGET) 						# 运行 make test 前，先确保 $(TARGET) 已经构建好   #test是一个自定义动作，意思是make test会依赖什么文件，然后执行下面的命令
	sh tests/run_tests.sh   

clean:
	rm -f $(TARGET)						
	rm -rf tests/data

clean-data:
	rm -rf tests/data

.PHONY: all test clean clean-data					# .PHONY 用来声明 test 和 clean 是伪目标，不是真实文件。声明为 .PHONY 后，Make 就总是执行这两个目标的命令，不管同名文件是否存在、是否过期。