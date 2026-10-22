/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Chinese (Simplified).
 *
 * Built-in Simplified Chinese UI catalog. No Traditional Chinese text is
 * presented as Simplified Chinese.
 *
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "I18N.h"

namespace i2p
{
namespace i18n
{
	namespace chinese_simplified
	{
		static std::string language = "chinese_simplified";

		static int plural (int)
		{
			return 0; // Chinese has one plural form
		}

		static bool rtl = false;

		static const LocaleStrings strings {
			{ "Status", "状态" },
			{ "Peers", "节点" },
			{ "Tunnels", "隧道" },
			{ "Torrents", "Torrent" },
			{ "NetDb", "NetDb" },
			{ "Logs", "日志" },
			{ "Services", "服务" },
			{ "Help", "帮助" },
			{ "Destinations", "目标" },
			{ "Config", "设置" },
			{ "Connections", "连接" },
			{ "connected", "已连接" },
			{ "disconnected", "已断开" },
			{ "yes", "是" },
			{ "no", "否" },
			{ "Router identity", "路由器标识" },
			{ "Uptime", "运行时间" },
			{ "Routers", "路由器" },
			{ "LeaseSets", "LeaseSet" },
			{ "Bandwidth", "带宽" },
			{ "Share ratio", "分享率" },
			{ "Network", "网络" },
			{ "Address", "地址" },
			{ "Version", "版本" },
			{ "Transit", "中转" },
			{ "In", "入站" },
			{ "Out", "出站" },
			{ "Router", "路由器" },
			{ "Floodfill", "Floodfill" },
			{ "Active", "活动" },
			{ "Idle", "空闲" },
			{ "Unknown", "未知" },
			{ "daemon is not connected", "守护进程未连接" },
			{ "connected to daemon", "已连接到守护进程" },
			{ "no torrent tunnel is available", "没有可用的 Torrent 隧道" },
			{ "no torrent selected", "未选择 Torrent" },
			{ "command is too long", "命令过长" },
			{ "Terminal is too small for an input prompt", "终端太小，无法显示输入框" },
			{ "Reseed the NetDb from the network? [y/N] ", "是否从网络重新播种 NetDb？[y/N] " },
			{ "Reload tunnel configuration? [y/N] ", "是否重新加载隧道配置？[y/N] " },
			{ "Stop the daemon immediately? Type SHUTDOWN to confirm: ", "是否立即停止守护进程？输入 SHUTDOWN 确认：" },
			{ "Begin graceful shutdown (stop accepting tunnels)? [y/N] ", "是否开始平稳关闭（停止接受隧道）？[y/N] " },
			{ "bandwidth class K/L/M/N/O/P/X: ", "带宽等级 K/L/M/N/O/P/X：" },
			{ "share ratio percent 0-100: ", "分享率百分比 0-100：" },
			{ "Terminal too small", "终端太小" },
			{ "Prompt suspended; Esc cancels", "输入已暂停；按 Esc 取消" },
			{ "Proxy error", "代理错误" },
			{ "Proxy info", "代理信息" },
			{ "Proxy error: Host not found", "代理错误：找不到主机" },
			{ "Remote host not found in router's addressbook", "路由器地址簿中找不到远程主机" },
			{ "You may try to find this host on jump services below", "您可以尝试在下面的跳转服务中查找此主机" },
			{ "Invalid request", "无效请求" },
			{ "Proxy unable to parse your request", "代理无法解析您的请求" },
			{ "Request too large", "请求过大" },
			{ "HTTP request header is too large", "HTTP 请求头过大" },
			{ "Addresshelper is not supported", "不支持 Addresshelper" },
			{ "Addresshelper forced update rejected", "Addresshelper 强制更新被拒绝" },
			{ "Addresshelper request", "Addresshelper 请求" },
			{ "Addresshelper adding", "Addresshelper 正在添加" },
			{ "Addresshelper update", "Addresshelper 更新" },
			{ "Invalid request URI", "无效的请求 URI" },
			{ "Can't detect destination host from request", "无法从请求中识别目标主机" },
			{ "Blocked target", "目标已被阻止" },
			{ "Loopback and private clearnet targets are not allowed", "不允许回环地址和私有明网目标" },
			{ "Outproxy failure", "出站代理失败" },
			{ "Bad outproxy settings", "出站代理设置错误" },
			{ "Unknown outproxy URL", "未知的出站代理 URL" },
			{ "Cannot resolve upstream proxy", "无法解析上游代理" },
			{ "Hostname is too long", "主机名过长" },
			{ "SOCKS proxy error", "SOCKS 代理错误" },
			{ "Cannot connect to upstream SOCKS proxy", "无法连接到上游 SOCKS 代理" },
			{ "Connection closed during handover", "交接期间连接已关闭" },
			{ "CONNECT error", "CONNECT 错误" },
			{ "Failed to connect", "连接失败" },
			{ "Failed to send request to upstream", "无法向上游发送请求" },
			{ "Cannot connect", "无法连接" },
			{ "HTTP out proxy not implemented", "未实现 HTTP 出站代理" },
			{ "Cannot connect to upstream HTTP proxy", "无法连接到上游 HTTP 代理" },
			{ "Host is down", "主机不可用" },
			{ "Can't create connection to requested host, it may be down. Please try again later.", "无法连接到请求的主机，它可能已离线。请稍后重试。" },
			{ "Clock skew", "时钟偏差" },
			{ "Offline", "离线" },
			{ "Symmetric NAT", "对称 NAT" },
			{ "Full cone NAT", "全锥形 NAT" },
			{ "No Descriptors", "无描述符" },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. <b>Be careful: ", "主机 %s <span class=\"warning\">已在路由器地址簿中</span>。<b>请注意：" },
			{ "To add host <b>%s</b> in router's addressbook, click here: <a ", "要将主机 <b>%s</b> 添加到路由器地址簿，请点击此处：<a " },
			{ "Host %s added to router's addressbook from helper. Click here to proceed: <a ", "主机 %s 已通过 helper 添加到路由器地址簿。点击此处继续：<a " },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. Click here to update ", "主机 %s <span class=\"warning\">已在路由器地址簿中</span>。点击此处更新 " },
			{ "Host %s is not inside I2P network, but outproxy is not enabled", "主机 %s 不在 I2P 网络内，但出站代理未启用" },
				{ "Version / net ID", "版本 / 网络 ID" },
			{ "Network status (IPv4)", "网络状态（IPv4）" },
			{ "Network status (IPv6)", "网络状态（IPv6）" },
			{ "Capabilities", "能力" },
			{ "NAT / testing", "NAT / 测试" },
			{ "NTCP2 / SSU2", "NTCP2 / SSU2" },
			{ "Transport online", "传输在线" },
			{ "Bandwidth in / out", "带宽 入/出" },
			{ "Bandwidth 15s in / out", "15 秒带宽 入/出" },
			{ "Transferred", "已传输" },
			{ "Transit bandwidth", "中转带宽" },
			{ "Transit transferred", "中转已传输" },
			{ "Peers / connections", "节点 / 连接" },
			{ "Routers / floodfills", "路由器 / 洪泛节点" },
			{ "Tunnels in / out / transit", "隧道 入/出/中转" },
			{ "Transit tunnels", "中转隧道" },
			{ "Tunnel creation success", "隧道创建成功率" },
			{ "Tunnel queues", "隧道队列" },
			{ "Reseeding", "重新播种" },
			{ "Daemon log level", "守护进程日志级别" },
			{ "Max bandwidth", "最大带宽" },
			{ "Bandwidth history (15-second samples)", "带宽历史（15 秒采样）" },
			{ "no torrents in this tunnel", "此隧道中没有 Torrent" },
			{ "no torrent matches the current filter", "没有 Torrent 匹配当前筛选" },
			{ "loading torrent snapshot...", "正在加载 Torrent 快照..." },
			{ "daemon is disconnected; torrent state is unavailable", "守护进程已断开；Torrent 状态不可用" },
			{ "reconnecting automatically - see the footer for details", "正在自动重连——请查看页脚" },
			{ "No torrent service tunnel is available on this router.", "此路由器没有可用的 Torrent 服务隧道。" },
			{ "Start a torrent client tunnel in openi2pd, then press r.", "请在 openi2pd 中启动 Torrent 客户端隧道，然后按 r。" },
			{ "Torrent administration does not open any files or network links here.", "Torrent 管理不会在此打开任何文件或网络链接。" },
			{ "no known peers", "没有已知节点" },
			{ "no tunnels", "没有隧道" },
			{ "no established transport connections", "没有已建立的传输连接" },
			{ "waiting for daemon", "正在等待守护进程" },
			{ "Name", "名称" },
			{ "Size", "大小" },
			{ "Prog", "进度" },
			{ "Down", "下载" },
			{ "Up", "上传" },
			{ "Ratio/ETA", "比率/预计" },
			{ "Error", "错误" },
	};

		static const std::map<std::string, std::vector<std::string>> plurals { };

		std::shared_ptr<const i2p::i18n::Locale> GetLocale ()
		{
			return std::make_shared<i2p::i18n::Locale> (
				language, rtl, strings, plurals, [] (int n) -> int { return plural (n); });
		}
	} // chinese_simplified
} // i18n
} // i2p
