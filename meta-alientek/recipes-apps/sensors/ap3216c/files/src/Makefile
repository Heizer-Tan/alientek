# 宿主机可选：分别 make -f Makefile.read / Makefile.logger
# Yocto 使用 recipe 内 do_compile，不依赖本文件
.PHONY: all clean
all:
	$(MAKE) -f Makefile.read
	$(MAKE) -f Makefile.logger
clean:
	$(MAKE) -f Makefile.read clean
	$(MAKE) -f Makefile.logger clean
