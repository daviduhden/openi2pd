/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Russian.
 *
 * Built-in Russian UI catalog with one/few/many plural forms.
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
	namespace russian
	{
		static std::string language = "russian";

		static int plural (int n)
		{
			const int mod10 = n % 10;
			const int mod100 = n % 100;
			if (mod10 == 1 && mod100 != 11) return 0;
			if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return 1;
			return 2;
		}

		static bool rtl = false;

		static const LocaleStrings strings {
			{ "Status", "Статус" },
			{ "Peers", "Пиры" },
			{ "Tunnels", "Туннели" },
			{ "Torrents", "Торренты" },
			{ "NetDb", "NetDb" },
			{ "Logs", "Журналы" },
			{ "Services", "Службы" },
			{ "Help", "Справка" },
			{ "Destinations", "Назначения" },
			{ "Config", "Настройки" },
			{ "Connections", "Подключения" },
			{ "connected", "подключено" },
			{ "disconnected", "отключено" },
			{ "yes", "да" },
			{ "no", "нет" },
			{ "Router identity", "Идентификатор роутера" },
			{ "Uptime", "Время работы" },
			{ "Routers", "Роутеры" },
			{ "LeaseSets", "LeaseSets" },
			{ "Bandwidth", "Пропускная способность" },
			{ "Share ratio", "Доля отдачи" },
			{ "Network", "Сеть" },
			{ "Address", "Адрес" },
			{ "Version", "Версия" },
			{ "Transit", "Транзит" },
			{ "In", "Вход" },
			{ "Out", "Выход" },
			{ "Router", "Роутер" },
			{ "Floodfill", "Floodfill" },
			{ "Active", "Активно" },
			{ "Idle", "Простой" },
			{ "Unknown", "Неизвестно" },
			{ "daemon is not connected", "демон не подключён" },
			{ "connected to daemon", "подключено к демону" },
			{ "no torrent tunnel is available", "нет доступного туннеля для торрентов" },
			{ "no torrent selected", "торрент не выбран" },
			{ "command is too long", "команда слишком длинная" },
			{ "Terminal is too small for an input prompt", "Терминал слишком мал для поля ввода" },
			{ "Reseed the NetDb from the network? [y/N] ", "Пересеять NetDb из сети? [д/Н] " },
			{ "Reload tunnel configuration? [y/N] ", "Перезагрузить конфигурацию туннелей? [д/Н] " },
			{ "Stop the daemon immediately? Type SHUTDOWN to confirm: ",
				"Немедленно остановить демон? Введите SHUTDOWN для подтверждения: " },
			{ "Begin graceful shutdown (stop accepting tunnels)? [y/N] ",
				"Начать корректное завершение (перестать принимать туннели)? [д/Н] " },
			{ "bandwidth class K/L/M/N/O/P/X: ", "класс пропускной способности K/L/M/N/O/P/X: " },
			{ "share ratio percent 0-100: ", "процент доли отдачи 0-100: " },
			{ "Terminal too small", "Терминал слишком мал" },
			{ "Prompt suspended; Esc cancels", "Ввод приостановлен; Esc отменяет" },
			{ "Proxy error", "Ошибка прокси" },
			{ "Proxy info", "Информация прокси" },
			{ "Proxy error: Host not found", "Ошибка прокси: хост не найден" },
			{ "Remote host not found in router's addressbook", "Удалённый хост не найден в адресной книге роутера" },
			{ "You may try to find this host on jump services below",
				"Попробуйте найти этот хост в сервисах перехода ниже" },
			{ "Invalid request", "Некорректный запрос" },
			{ "Proxy unable to parse your request", "Прокси не может разобрать запрос" },
			{ "Request too large", "Запрос слишком большой" },
			{ "HTTP request header is too large", "Заголовок HTTP слишком большой" },
			{ "Addresshelper is not supported", "Addresshelper не поддерживается" },
			{ "Addresshelper forced update rejected", "Принудительное обновление Addresshelper отклонено" },
			{ "Addresshelper request", "Запрос Addresshelper" },
			{ "Addresshelper adding", "Addresshelper добавляет" },
			{ "Addresshelper update", "Обновление Addresshelper" },
			{ "Invalid request URI", "Некорректный URI запроса" },
			{ "Can't detect destination host from request", "Не удаётся определить хост назначения из запроса" },
			{ "Blocked target", "Заблокированная цель" },
			{ "Loopback and private clearnet targets are not allowed",
				"Цели loopback и частные цели clearnet запрещены" },
			{ "Outproxy failure", "Сбой исходящего прокси" },
			{ "Bad outproxy settings", "Неверные настройки исходящего прокси" },
			{ "Unknown outproxy URL", "Неизвестный URL исходящего прокси" },
			{ "Cannot resolve upstream proxy", "Не удаётся разрешить вышестоящий прокси" },
			{ "Hostname is too long", "Имя хоста слишком длинное" },
			{ "SOCKS proxy error", "Ошибка прокси SOCKS" },
			{ "Cannot connect to upstream SOCKS proxy", "Не удаётся подключиться к вышестоящему прокси SOCKS" },
			{ "Connection closed during handover", "Соединение закрыто при передаче" },
			{ "CONNECT error", "Ошибка CONNECT" },
			{ "Failed to connect", "Не удалось подключиться" },
			{ "Failed to send request to upstream", "Не удалось отправить запрос вышестоящему прокси" },
			{ "Cannot connect", "Невозможно подключиться" },
			{ "HTTP out proxy not implemented", "Исходящий HTTP-прокси не реализован" },
			{ "Cannot connect to upstream HTTP proxy", "Не удаётся подключиться к вышестоящему HTTP-прокси" },
			{ "Host is down", "Хост недоступен" },
			{ "Can't create connection to requested host, it may be down. Please try again later.",
				"Не удаётся создать соединение с запрошенным хостом; возможно, он недоступен. Повторите попытку "
				"позже." },
			{ "Clock skew", "Расхождение часов" },
			{ "Offline", "Не в сети" },
			{ "Symmetric NAT", "Симметричный NAT" },
			{ "Full cone NAT", "NAT полного конуса" },
			{ "No Descriptors", "Нет дескрипторов" },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. <b>Be careful: ",
				"Хост %s <span class=\"warning\">уже есть в адресной книге роутера</span>. <b>Будьте осторожны: " },
			{ "To add host <b>%s</b> in router's addressbook, click here: <a ",
				"Чтобы добавить хост <b>%s</b> в адресную книгу роутера, нажмите здесь: <a " },
			{ "Host %s added to router's addressbook from helper. Click here to proceed: <a ",
				"Хост %s добавлен в адресную книгу роутера из helper. Нажмите здесь, чтобы продолжить: <a " },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. Click here to update ",
				"Хост %s <span class=\"warning\">уже есть в адресной книге роутера</span>. Нажмите здесь, чтобы "
				"обновить " },
			{ "Host %s is not inside I2P network, but outproxy is not enabled",
				"Хост %s не находится в сети I2P, но исходящий прокси не включён" },
			{ "Version / net ID", "Версия / сетевой ID" },
			{ "Network status (IPv4)", "Состояние сети (IPv4)" },
			{ "Network status (IPv6)", "Состояние сети (IPv6)" },
			{ "Capabilities", "Возможности" },
			{ "NAT / testing", "NAT / тестирование" },
			{ "NTCP2 / SSU2", "NTCP2 / SSU2" },
			{ "Transport online", "Транспорт онлайн" },
			{ "Bandwidth in / out", "Пропускная способность вход/выход" },
			{ "Bandwidth 15s in / out", "Пропускная способность 15 с вход/выход" },
			{ "Transferred", "Передано" },
			{ "Transit bandwidth", "Пропускная способность транзита" },
			{ "Transit transferred", "Передано транзитом" },
			{ "Peers / connections", "Пиры / подключения" },
			{ "Routers / floodfills", "Роутеры / флудфиллы" },
			{ "Tunnels in / out / transit", "Туннели вход/выход/транзит" },
			{ "Transit tunnels", "Транзитные туннели" },
			{ "Tunnel creation success", "Успешность создания туннелей" },
			{ "Tunnel queues", "Очереди туннелей" },
			{ "Reseeding", "Пересев" },
			{ "Daemon log level", "Уровень журнала демона" },
			{ "Max bandwidth", "Максимальная пропускная способность" },
			{ "Bandwidth history (15-second samples)", "История пропускной способности (15 с)" },
			{ "no torrents in this tunnel", "в этом туннеле нет торрентов" },
			{ "no torrent matches the current filter", "ни один торрент не соответствует фильтру" },
			{ "loading torrent snapshot...", "загрузка снимка торрентов..." },
			{ "daemon is disconnected; torrent state is unavailable",
				"демон отключён; состояние торрентов недоступно" },
			{ "reconnecting automatically - see the footer for details",
				"автоматическое переподключение — см. нижнюю строку" },
			{ "No torrent service tunnel is available on this router.",
				"На этом роутере нет доступного туннеля торрент-сервиса." },
			{ "Start a torrent client tunnel in openi2pd, then press r.",
				"Запустите клиентский торрент-туннель в openi2pd, затем нажмите r." },
			{ "Torrent administration does not open any files or network links here.",
				"Администрирование торрентов не открывает здесь файлы или сетевые ссылки." },
			{ "no known peers", "нет известных пиров" },
			{ "no tunnels", "нет туннелей" },
			{ "no established transport connections", "нет установленных транспортных соединений" },
			{ "waiting for daemon", "ожидание демона" },
			{ "Name", "Имя" },
			{ "Size", "Размер" },
			{ "Prog", "Прогр." },
			{ "Down", "Загрузка" },
			{ "Up", "Отдача" },
			{ "Ratio/ETA", "Рейтинг/ETA" },
			{ "Error", "Ошибка" },
		};

		static const std::map<std::string, std::vector<std::string>> plurals { };

		std::shared_ptr<const i2p::i18n::Locale> GetLocale ()
		{
			return std::make_shared<i2p::i18n::Locale> (
				language, rtl, strings, plurals, [] (int n) -> int { return plural (n); });
		}
	} // russian
} // i18n
} // i2p
