# AP3216C 采集入库与 board-web Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在板上提供 `ap3216c-logger`（每 5 分钟写 SQLite）与通用服务 `board-web`（:8080 表格+折线图查询），并装入 `alientek-image-base`。

**Architecture:** 双进程解耦。logger 读 `/dev/ap3216c` 解析 `ir/als/ps` 写入 `/var/lib/ap3216c/ap3216c.db`，并删除 7 天前数据。board-web 用最小 C socket HTTP 提供 `GET /` 与 `GET /api/samples`，前端内联 HTML/JS 拉 JSON 画表与图。SysV 默认自启。

**Tech Stack:** C99、libsqlite3、SysV init、Yocto scarthgap recipe；无 Python/无第三方 HTTP 库（避免额外许可与 fetch）

## Global Constraints

- 采集间隔固定 300 秒；保留最近 7 天
- DB 路径：`/var/lib/ap3216c/ap3216c.db`
- Web 名：`board-web`，监听 `0.0.0.0:8080`，无登录
- 不改内核驱动、不废弃 `ap3216c-read`、不用 systemd
- 注释中文；函数尽量 ≤30 行；显式错误处理
- Recipe 布局参考 `key-monitor`：`S=${WORKDIR}/src`，`do_install` 用 `install` 不用 `oe_runmake install`
- 设计文档：`docs/superpowers/specs/2026-09-06-sensor-board-web-design.md`

## File map

| 文件 | 职责 |
|------|------|
| `meta-alientek/recipes-apps/ap3216c-logger/files/src/ap3216c-logger.c` | 采样循环、解析、SQLite 写/清理 |
| `meta-alientek/recipes-apps/ap3216c-logger/files/src/Makefile` | 链接 `-lsqlite3` |
| `meta-alientek/recipes-apps/ap3216c-logger/files/ap3216c-logger.init` | SysV，默认可 enable |
| `meta-alientek/recipes-apps/ap3216c-logger/ap3216c-logger_1.0.bb` | 打包 + update-rc.d |
| `meta-alientek/recipes-apps/board-web/files/src/board-web.c` | HTTP + SQLite 查询 |
| `meta-alientek/recipes-apps/board-web/files/src/index.html` | 查询页（可嵌入 C 字符串或安装到 `/usr/share/board-web/`） |
| `meta-alientek/recipes-apps/board-web/files/src/Makefile` | 链接 `-lsqlite3` |
| `meta-alientek/recipes-apps/board-web/files/board-web.init` | SysV |
| `meta-alientek/recipes-apps/board-web/board-web_1.0.bb` | 打包 |
| `meta-alientek/recipes-core/images/alientek-image-base.bb` | 安装包 |
| `README.md` | 用法 |

---

### Task 1: ap3216c-logger（采集 + 入库 + SysV）

**Files:**
- Create: `meta-alientek/recipes-apps/ap3216c-logger/files/src/ap3216c-logger.c`
- Create: `meta-alientek/recipes-apps/ap3216c-logger/files/src/Makefile`
- Create: `meta-alientek/recipes-apps/ap3216c-logger/files/ap3216c-logger.init`
- Create: `meta-alientek/recipes-apps/ap3216c-logger/ap3216c-logger_1.0.bb`

**Interfaces:**
- Consumes: `/dev/ap3216c` 文本行 `ir=%u als=%u ps=%u`
- Produces: DB `/var/lib/ap3216c/ap3216c.db` 表 `samples(id,ts,ir,als,ps)`；二进制 `/usr/bin/ap3216c-logger`

- [ ] **Step 1: 写宿主机可编译的解析小测（先失败）**

在临时文件或同目录加 `parse_sample` 纯函数，宿主机用：

```c
/* 期望：parseSample("ir=1 als=2 ps=3\n", &ir,&als,&ps)==0 */
```

Run: 先写测试 main 调用尚未实现的解析 → 编译失败或断言失败。

- [ ] **Step 2: 实现 logger 核心**

关键常量与行为：

```c
#define AP3216C_DEV_PATH "/dev/ap3216c"
#define AP3216C_DB_PATH  "/var/lib/ap3216c/ap3216c.db"
#define AP3216C_DB_DIR   "/var/lib/ap3216c"
#define SAMPLE_INTERVAL_SEC 300
#define RETAIN_SECONDS (7 * 86400)
```

必须实现的函数（输入输出类型明确）：

```c
static int ensureDbDir(void); /* 0 成功，非 0 失败 */
static int openDb(sqlite3 **db); /* 建表+索引 */
static int parseSample(const char *line, unsigned *ir, unsigned *als, unsigned *ps);
static int readDeviceSample(unsigned *ir, unsigned *als, unsigned *ps);
static int insertSample(sqlite3 *db, time_t ts, unsigned ir, unsigned als, unsigned ps);
static int purgeOldSamples(sqlite3 *db, time_t now);
static int sampleOnce(sqlite3 *db); /* 读+写+清理；失败记 syslog 返回非 0 */
```

主循环：`sampleOnce` → `sleep(300)`；`SIGINT/SIGTERM` 优雅关库退出。  
读设备：`open`/`read`/`lseek(0,SEEK_SET)`/`close` 每周期可 open-close，或保持 fd 并 lseek。  
SQL：

```sql
CREATE TABLE IF NOT EXISTS samples (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  ir INTEGER NOT NULL,
  als INTEGER NOT NULL,
  ps INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts);
INSERT INTO samples(ts,ir,als,ps) VALUES(?,?,?,?);
DELETE FROM samples WHERE ts < ?;
```

注释中文；用 `syslog(LOG_ERR/LOG_INFO, ...)`。

- [ ] **Step 3: Makefile**

```makefile
TARGET = ap3216c-logger
SRCS = ap3216c-logger.c
CFLAGS ?= -Wall -O2
LDFLAGS ?=
LDLIBS = -lsqlite3

all: $(TARGET)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f $(TARGET)
```

- [ ] **Step 4: SysV init（默认自启）**

参考 `key-monitor.init`，但 `Default-Start: 2 3 4 5`，`Default-Stop: 0 1 6`；`DAEMON=/usr/bin/ap3216c-logger`，无额外参数。

- [ ] **Step 5: Recipe**

```bitbake
SUMMARY = "AP3216C 定时采样写入 SQLite"
LICENSE = "MIT"
# LIC_FILES_CHKSUM 按 SPDX 头生成

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"

SRC_URI = " \
    file://src/ap3216c-logger.c \
    file://src/Makefile \
    file://ap3216c-logger.init \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "ap3216c-logger"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o ap3216c-logger ap3216c-logger.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ap3216c-logger ${D}${bindir}/ap3216c-logger
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/ap3216c-logger.init ${D}${sysconfdir}/init.d/ap3216c-logger
    install -d ${D}/var/lib/ap3216c
}

FILES:${PN} += "${sysconfdir}/init.d/ap3216c-logger /var/lib/ap3216c"
```

- [ ] **Step 6: 验证**

Run: `./scripts/build.sh ap3216c-logger`  
Expected: 编译打包成功。

宿主机若有 sqlite3 开发包，可先 `make` 通过（设备读会失败，属正常）。

- [ ] **Step 7: Commit**

```bash
git add meta-alientek/recipes-apps/ap3216c-logger
git commit -m "feat: 增加 ap3216c-logger 定时采样入库"
```

---

### Task 2: board-web（HTTP API + 查询页）

**Files:**
- Create: `meta-alientek/recipes-apps/board-web/files/src/board-web.c`
- Create: `meta-alientek/recipes-apps/board-web/files/src/index.html`
- Create: `meta-alientek/recipes-apps/board-web/files/src/Makefile`
- Create: `meta-alientek/recipes-apps/board-web/files/board-web.init`
- Create: `meta-alientek/recipes-apps/board-web/board-web_1.0.bb`

**Interfaces:**
- Consumes: 同一 DB 路径与 `samples` 表
- Produces: `GET /` HTML；`GET /api/samples?from=&to=` JSON 数组

- [ ] **Step 1: 固定 API 契约（先写静态期望样例）**

成功响应：

```http
HTTP/1.1 200 OK
Content-Type: application/json

[{"ts":1725600000,"ir":2,"als":7,"ps":360}]
```

非法参数 → `400`；DB 错误 → `500`；无数据 → `[]`。

- [ ] **Step 2: 实现最小 HTTP 服务**

监听 `0.0.0.0:8080`。仅支持 `GET`。路由：

- `/` → 读 `/usr/share/board-web/index.html`（或编译期嵌入）；`Content-Type: text/html`
- `/api/samples` → 解析 query `from`/`to`（缺省：`to=now`，`from=now-86400`）；`from>to` 或非数字 → 400

查询 SQL：

```sql
SELECT ts, ir, als, ps FROM samples
 WHERE ts >= ? AND ts <= ?
 ORDER BY ts ASC;
```

表格页用 JS 再按倒序展示；图用升序时间轴。

实现要点：

```c
#define BOARD_WEB_PORT 8080
#define BOARD_WEB_DB_PATH "/var/lib/ap3216c/ap3216c.db"
#define BOARD_WEB_INDEX "/usr/share/board-web/index.html"

static int parseQueryTime(const char *query, time_t *fromTs, time_t *toTs);
static int querySamplesJson(sqlite3 *db, time_t fromTs, time_t toTs, char **outJson, size_t *outLen);
static void handleClient(int clientFd, sqlite3 *db);
```

单连接顺序处理即可（YAGNI：不做线程池）。`Content-Length` 必填。

- [ ] **Step 3: index.html（表格 + 折线图）**

页面要求：

- 标题如 “板端传感器数据”
- `from`/`to` 用 datetime-local 或两个输入；默认最近 24h
- “查询”按钮 → `fetch('/api/samples?from=&to=')`
- 表格列：时间、IR、ALS、PS（时间本地化显示）
- canvas 折线图三条线 + 图例；无数据时显示“暂无数据”
- 不依赖外网 CDN（全部内联 JS/CSS）

- [ ] **Step 4: Makefile / init / recipe**

Makefile 同 logger，目标 `board-web`，`-lsqlite3`。

Init：`board-web`，defaults 自启。

Recipe：

```bitbake
SUMMARY = "通用板端 Web：查询传感器历史"
DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"
inherit update-rc.d
INITSCRIPT_NAME = "board-web"
INITSCRIPT_PARAMS = "defaults"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/board-web ${D}${bindir}/board-web
    install -d ${D}${datadir}/board-web
    install -m 0644 ${S}/index.html ${D}${datadir}/board-web/index.html
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/board-web.init ${D}${sysconfdir}/init.d/board-web
}
FILES:${PN} += "${datadir}/board-web ${sysconfdir}/init.d/board-web"
```

- [ ] **Step 5: 验证**

Run: `./scripts/build.sh board-web`  
Expected: 成功。

可选宿主机：临时 DB 插入几行后跑 `board-web`，`curl localhost:8080/api/samples` 见 JSON。

- [ ] **Step 6: Commit**

```bash
git add meta-alientek/recipes-apps/board-web
git commit -m "feat: 增加 board-web 传感器历史查询服务"
```

---

### Task 3: 镜像集成与 README

**Files:**
- Modify: `meta-alientek/recipes-core/images/alientek-image-base.bb`
- Modify: `README.md`

**Interfaces:**
- Consumes: Task 1/2 包名
- Produces: 镜像含 logger、board-web、sqlite3

- [ ] **Step 1: 镜像安装**

在 `CORE_IMAGE_EXTRA_INSTALL` 增加：

```bitbake
    ap3216c-logger \
    board-web \
    sqlite3 \
```

- [ ] **Step 2: README 小节**

内容至少包括：

```md
## 传感器入库与 Web 查询

- 采集：`ap3216c-logger` 每 5 分钟写入 `/var/lib/ap3216c/ap3216c.db`，保留 7 天
- 查询：浏览器打开 `http://<板子IP>:8080/`
- 启停：`/etc/init.d/ap3216c-logger start|stop`；`/etc/init.d/board-web start|stop`
- 手动读数仍可用：`ap3216c-read`
```

- [ ] **Step 3: 构建**

Run: `./scripts/build.sh alientek-image-base`  
Expected: rootfs 含 `/usr/bin/ap3216c-logger`、`/usr/bin/board-web`、`/usr/share/board-web/index.html`，rc 链接存在。

- [ ] **Step 4: Commit**

```bash
git add meta-alientek/recipes-core/images/alientek-image-base.bb README.md
git commit -m "feat: 镜像集成传感器采集与 board-web"
```

---

### Task 4: 板级验收清单

不写新代码；导出 NFS/TFTP 后验证：

- [ ] **Step 1:** `ps | grep -E 'ap3216c-logger|board-web'` 均在跑
- [ ] **Step 2:** `sqlite3 /var/lib/ap3216c/ap3216c.db 'SELECT * FROM samples ORDER BY id DESC LIMIT 5;'` 有数据（可先等一轮或临时把间隔改为测试值验证后改回 300）
- [ ] **Step 3:** PC 浏览器打开 `http://192.168.5.201:8080/`（按实际 IP）见表格与图
- [ ] **Step 4:** 改时间范围后数据变化；`ap3216c-read` 仍可用

若 5 分钟等待过长，开发阶段可用环境变量或编译宏 `SAMPLE_INTERVAL_SEC` 临时改为 30 做联调，**合并前必须回到 300**。

---

## Spec coverage check

| Spec 项 | Task |
|---------|------|
| 5 分钟采集 | Task 1 |
| SQLite + 7 天清理 | Task 1 |
| board-web :8080 表格+图 | Task 2 |
| `/api/samples` | Task 2 |
| 默认 SysV 自启 | Task 1/2 |
| 镜像 + README | Task 3 |
| 验收 | Task 4 |
| 不改驱动 / 保留 ap3216c-read | 全任务遵守 |
