# 宿主机可选：分别 make -f Makefile.read / Makefile.logger
# Yocto 使用 recipe 内 do_compile；iio-icm 需从 ../../common 拷入或设置 COMMON
COMMON_SRC ?= ../../../../common
.PHONY: all clean
all:
	@test -f iio-icm.c || cp -f $(COMMON_SRC)/iio-icm.c $(COMMON_SRC)/iio-icm.h .
	$(MAKE) -f Makefile.read
	$(MAKE) -f Makefile.logger
clean:
	$(MAKE) -f Makefile.read clean
	$(MAKE) -f Makefile.logger clean
	rm -f iio-icm.c iio-icm.h
