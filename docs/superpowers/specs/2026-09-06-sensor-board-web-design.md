# AP3216C 采集入库与 board-web 查询设计

日期：2026-09-06  
状态：已对话确认，待落地实现  
范围：板端每 5 分钟采集 AP3216C，写入 SQLite；经通用 Web 服务 `board-web` 提供表格与折线图查询

## 1. 背景与目标

阿尔法板已有 out-of-tree 驱动 `/dev/ap3216c` 与手动工具 `ap3216c-read`。尚无定时落库与浏览器查询能力。

**目标**：

1. 板上常驻采集，默认间隔 **5 分钟**
2. 数据写入本地 **SQLite**
3. 通过局域网浏览器查询：**表格 + 折线图**
4. 默认只保留最近 **7 天** 数据并自动清理
5. 实现语言：**C**；依赖尽量少，易集成进 `alientek-image-base`

**非目标**：

- 登录鉴权、HTTPS
- 多板汇总、云端上报
- 替换或废弃现有 `ap3216c-read`
- 修改内核驱动接口
- systemd

## 2. 架构

```text
/dev/ap3216c → ap3216c-logger（每 5 分钟）→ SQLite
                                              ↑
浏览器 ← board-web（:8080，表格+折线图+JSON）┘
```

| 组件 | 路径（拟） | 职责 |
|------|------------|------|
| `ap3216c-logger` | `meta-alientek/recipes-apps/ap3216c-logger/` | 定时采样、写库、清理过期数据 |
| `board-web` | `meta-alientek/recipes-apps/board-web/` | HTTP 服务与查询页（通用命名，不绑定传感器型号） |
| SQLite 文件 | `/var/lib/ap3216c/ap3216c.db` | 采样存储 |

采用**双进程**：采集与 Web 解耦，便于单独启停与排障。

## 3. 数据模型

```sql
CREATE TABLE samples (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,   -- Unix 秒（UTC）
  ir INTEGER NOT NULL,
  als INTEGER NOT NULL,
  ps INTEGER NOT NULL
);
CREATE INDEX idx_samples_ts ON samples(ts);
```

- 数据源：打开 `/dev/ap3216c`，读取一行文本 `ir=%u als=%u ps=%u`，解析后入库
- 清理：每次成功写入后执行 `DELETE FROM samples WHERE ts < strftime('%s','now') - 7*86400`
- 目录：`/var/lib/ap3216c/` 由 logger 启动时创建（权限适合 root 运行）

## 4. ap3216c-logger

- 常驻进程；启动后**立即采一次**，之后每 **300 秒**采一次
- 读失败：写 syslog，本周期跳过，进程不退出
- 写库失败：写 syslog，本周期跳过
- SysV init：`/etc/init.d/ap3216c-logger`，**默认开机自启**
- 依赖：`libsqlite3`、现有 `ap3216c` 字符设备

## 5. board-web

- 监听 `0.0.0.0:8080`
- 无登录（局域网调试场景）
- SysV init：`/etc/init.d/board-web`，**默认开机自启**
- HTTP：C 内嵌轻量实现（优先单文件/最小依赖方案，如嵌入式 HTTP 库或自写最小解析）；运行依赖主要为 `libsqlite3`

### 5.1 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 查询页：时间范围筛选、表格、折线图 |
| GET | `/api/samples?from=&to=` | JSON；`from`/`to` 为 Unix 秒；缺省为最近 24 小时 |

JSON 元素示例：

```json
{"ts":1725600000,"ir":2,"als":7,"ps":360}
```

### 5.2 页面行为

- 默认展示最近 24 小时
- 表格：按 `ts` 倒序
- 折线图：同一时间轴上 IR / ALS / PS 三条线（轻量前端，可内联静态资源）
- 时间范围由页面控件提交，刷新表格与图

## 6. Yocto 集成

- 两个 recipe：`ap3216c-logger`、`board-web`
- 镜像 `CORE_IMAGE_EXTRA_INSTALL` 增加：`ap3216c-logger`、`board-web`、`sqlite3`
- `DEPENDS` / `RDEPENDS` 正确声明 `sqlite3`
- README 补充：服务启停、访问 URL、默认保留 7 天、与 `ap3216c-read` 并存说明

## 7. 错误处理

| 场景 | 行为 |
|------|------|
| 设备节点不存在 / 读失败 | logger 记 syslog，继续下一周期 |
| DB 不可写 | logger/web 记错误；web 对 API 返回明确 HTTP 错误 |
| 非法 `from`/`to` | API 返回 400 |
| 无数据 | 空列表 + 页面提示“暂无数据” |

## 8. 验收标准

1. 开机后 `ap3216c-logger`、`board-web` 进程在跑
2. 约 5 分钟内库中出现新采样；可用 `sqlite3` 查到 `ir/als/ps`
3. PC 浏览器打开 `http://<板子IP>:8080/`，可见表格与折线图
4. 时间范围筛选生效
5. 超过 7 天的旧记录会被清理
6. 手动 `ap3216c-read` 仍可用

## 9. 实现顺序（概要）

1. SQLite schema + `ap3216c-logger` + SysV
2. `board-web` API + 静态页（表格+图）+ SysV
3. 镜像与 README
4. 板级验收

详细步骤见后续 implementation plan。
