# 故障分段

1. U-Boot 无输出：串口设备、波特率、拨码、`dd`/`bmaptool` 是否写对盘
2. 停在 U-Boot：DRAM 512MB、mmc 设备号
3. 内核 panic 无根：先 mmc 根，再 nfs 根，对比 `printenv bootargs`
4. 单网口：KSZ8081 reset（GPIO5_IO7/8）、MDIO 地址 2/1
5. NFS：板端 ping `serverip` → TFTP 能否取 zImage → export 与 `no_root_squash` → `nfsvers=3`

相关文档：

- [烧写 TF](./flash-tf.md)
- [NFS](./nfs.md)
- [编译 / kas 镜像](./build.md)
