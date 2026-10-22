/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Spanish.
 *
 * Built-in Spanish UI catalog. Keys are the English source strings; formatting
 * placeholders and HTML markup are preserved byte-for-byte.
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
	namespace spanish
	{
		static std::string language = "spanish";

		static int plural (int n)
		{
			return n != 1 ? 1 : 0;
		}

		static bool rtl = false;

		static const LocaleStrings strings {
			{ "Status", "Estado" },
			{ "Peers", "Pares" },
			{ "Tunnels", "Túneles" },
			{ "Torrents", "Torrents" },
			{ "NetDb", "NetDb" },
			{ "Logs", "Registros" },
			{ "Services", "Servicios" },
			{ "Help", "Ayuda" },
			{ "Destinations", "Destinos" },
			{ "Config", "Configuración" },
			{ "Connections", "Conexiones" },
			{ "connected", "conectado" },
			{ "disconnected", "desconectado" },
			{ "yes", "sí" },
			{ "no", "no" },
			{ "Router identity", "Identidad del router" },
			{ "Uptime", "Tiempo activo" },
			{ "Routers", "Routers" },
			{ "LeaseSets", "LeaseSets" },
			{ "Bandwidth", "Ancho de banda" },
			{ "Share ratio", "Proporción compartida" },
			{ "Network", "Red" },
			{ "Address", "Dirección" },
			{ "Version", "Versión" },
			{ "Transit", "Tránsito" },
			{ "In", "Entrada" },
			{ "Out", "Salida" },
			{ "Router", "Router" },
			{ "Floodfill", "Floodfill" },
			{ "Active", "Activo" },
			{ "Idle", "Inactivo" },
			{ "Unknown", "Desconocido" },
			{ "daemon is not connected", "el daemon no está conectado" },
			{ "connected to daemon", "conectado al daemon" },
			{ "no torrent tunnel is available", "no hay un túnel de torrent disponible" },
			{ "no torrent selected", "ningún torrent seleccionado" },
			{ "command is too long", "el comando es demasiado largo" },
			{ "Terminal is too small for an input prompt",
				"La terminal es demasiado pequeña para un cuadro de entrada" },
			{ "Reseed the NetDb from the network? [y/N] ", "¿Volver a sembrar NetDb desde la red? [s/N] " },
			{ "Reload tunnel configuration? [y/N] ", "¿Recargar la configuración de túneles? [s/N] " },
			{ "Stop the daemon immediately? Type SHUTDOWN to confirm: ",
				"¿Detener el daemon inmediatamente? Escriba SHUTDOWN para confirmar: " },
			{ "Begin graceful shutdown (stop accepting tunnels)? [y/N] ",
				"¿Iniciar un apagado ordenado (dejar de aceptar túneles)? [s/N] " },
			{ "bandwidth class K/L/M/N/O/P/X: ", "clase de ancho de banda K/L/M/N/O/P/X: " },
			{ "share ratio percent 0-100: ", "porcentaje de proporción compartida 0-100: " },
			{ "Terminal too small", "Terminal demasiado pequeña" },
			{ "Prompt suspended; Esc cancels", "Entrada suspendida; Esc cancela" },
			{ "Proxy error", "Error del proxy" },
			{ "Proxy info", "Información del proxy" },
			{ "Proxy error: Host not found", "Error del proxy: host no encontrado" },
			{ "Remote host not found in router's addressbook",
				"El host remoto no está en la libreta de direcciones del router" },
			{ "You may try to find this host on jump services below",
				"Puede intentar encontrar este host en los servicios de salto siguientes" },
			{ "Invalid request", "Solicitud no válida" },
			{ "Proxy unable to parse your request", "El proxy no puede analizar la solicitud" },
			{ "Request too large", "Solicitud demasiado grande" },
			{ "HTTP request header is too large", "La cabecera HTTP es demasiado grande" },
			{ "Addresshelper is not supported", "Addresshelper no es compatible" },
			{ "Addresshelper forced update rejected", "Actualización forzada de Addresshelper rechazada" },
			{ "Addresshelper request", "Solicitud de Addresshelper" },
			{ "Addresshelper adding", "Addresshelper añadiendo" },
			{ "Addresshelper update", "Actualización de Addresshelper" },
			{ "Invalid request URI", "URI de solicitud no válida" },
			{ "Can't detect destination host from request", "No se puede detectar el host de destino de la solicitud" },
			{ "Blocked target", "Destino bloqueado" },
			{ "Loopback and private clearnet targets are not allowed",
				"No se permiten destinos loopback ni privados de la red pública" },
			{ "Outproxy failure", "Fallo del proxy de salida" },
			{ "Bad outproxy settings", "Configuración del proxy de salida incorrecta" },
			{ "Unknown outproxy URL", "URL del proxy de salida desconocida" },
			{ "Cannot resolve upstream proxy", "No se puede resolver el proxy ascendente" },
			{ "Hostname is too long", "El nombre de host es demasiado largo" },
			{ "SOCKS proxy error", "Error del proxy SOCKS" },
			{ "Cannot connect to upstream SOCKS proxy", "No se puede conectar al proxy SOCKS ascendente" },
			{ "Connection closed during handover", "Conexión cerrada durante la transferencia" },
			{ "CONNECT error", "Error de CONNECT" },
			{ "Failed to connect", "No se pudo conectar" },
			{ "Failed to send request to upstream", "No se pudo enviar la solicitud al proxy ascendente" },
			{ "Cannot connect", "No se puede conectar" },
			{ "HTTP out proxy not implemented", "Proxy de salida HTTP no implementado" },
			{ "Cannot connect to upstream HTTP proxy", "No se puede conectar al proxy HTTP ascendente" },
			{ "Host is down", "El host está caído" },
			{ "Can't create connection to requested host, it may be down. Please try again later.",
				"No se puede crear una conexión con el host solicitado; puede estar caído. Inténtelo de nuevo más "
				"tarde." },
			{ "Clock skew", "Desfase de reloj" },
			{ "Offline", "Sin conexión" },
			{ "Symmetric NAT", "NAT simétrico" },
			{ "Full cone NAT", "NAT de cono completo" },
			{ "No Descriptors", "Sin descriptores" },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. <b>Be careful: ",
				"El host %s <span class=\"warning\">ya está en la libreta de direcciones del router</span>. <b>Tenga "
				"cuidado: " },
			{ "To add host <b>%s</b> in router's addressbook, click here: <a ",
				"Para añadir el host <b>%s</b> a la libreta de direcciones del router, pulse aquí: <a " },
			{ "Host %s added to router's addressbook from helper. Click here to proceed: <a ",
				"Host %s añadido a la libreta de direcciones del router desde el helper. Pulse aquí para continuar: "
				"<a " },
			{ "Host %s is <span class=\"warning\">already in router's addressbook</span>. Click here to update ",
				"El host %s <span class=\"warning\">ya está en la libreta de direcciones del router</span>. Pulse aquí "
				"para actualizar " },
			{ "Host %s is not inside I2P network, but outproxy is not enabled",
				"El host %s no está dentro de la red I2P, pero el proxy de salida no está habilitado" },
			{ "Version / net ID", "Versión / ID de red" },
			{ "Network status (IPv4)", "Estado de red (IPv4)" },
			{ "Network status (IPv6)", "Estado de red (IPv6)" },
			{ "Capabilities", "Capacidades" },
			{ "NAT / testing", "NAT / prueba" },
			{ "NTCP2 / SSU2", "NTCP2 / SSU2" },
			{ "Transport online", "Transporte en línea" },
			{ "Bandwidth in / out", "Ancho de banda ent./sal." },
			{ "Bandwidth 15s in / out", "Ancho de banda 15 s ent./sal." },
			{ "Transferred", "Transferido" },
			{ "Transit bandwidth", "Ancho de banda de tránsito" },
			{ "Transit transferred", "Tránsito transferido" },
			{ "Peers / connections", "Pares / conexiones" },
			{ "Routers / floodfills", "Routers / floodfills" },
			{ "Tunnels in / out / transit", "Túneles ent./sal./tránsito" },
			{ "Transit tunnels", "Túneles de tránsito" },
			{ "Tunnel creation success", "Éxito de creación de túneles" },
			{ "Tunnel queues", "Colas de túneles" },
			{ "Reseeding", "Resembrando" },
			{ "Daemon log level", "Nivel de registro del daemon" },
			{ "Max bandwidth", "Ancho de banda máximo" },
			{ "Bandwidth history (15-second samples)", "Historial de ancho de banda (muestras de 15 s)" },
			{ "no torrents in this tunnel", "no hay torrents en este túnel" },
			{ "no torrent matches the current filter", "ningún torrent coincide con el filtro" },
			{ "loading torrent snapshot...", "cargando instantánea de torrents..." },
			{ "daemon is disconnected; torrent state is unavailable",
				"el daemon está desconectado; el estado de torrents no está disponible" },
			{ "reconnecting automatically - see the footer for details",
				"reconectando automáticamente; consulte el pie de página" },
			{ "No torrent service tunnel is available on this router.",
				"No hay ningún túnel de servicio de torrents disponible en este router." },
			{ "Start a torrent client tunnel in openi2pd, then press r.",
				"Inicie un túnel de cliente de torrents en openi2pd y pulse r." },
			{ "Torrent administration does not open any files or network links here.",
				"La administración de torrents no abre archivos ni enlaces de red aquí." },
			{ "no known peers", "no hay pares conocidos" },
			{ "no tunnels", "no hay túneles" },
			{ "no established transport connections", "no hay conexiones de transporte establecidas" },
			{ "waiting for daemon", "esperando al daemon" },
			{ "Name", "Nombre" },
			{ "Size", "Tamaño" },
			{ "Prog", "Prog." },
			{ "Down", "Bajada" },
			{ "Up", "Subida" },
			{ "Ratio/ETA", "Proporción/ETA" },
			{ "Error", "Error" },
		};

		static const std::map<std::string, std::vector<std::string>> plurals { };

		std::shared_ptr<const i2p::i18n::Locale> GetLocale ()
		{
			return std::make_shared<i2p::i18n::Locale> (
				language, rtl, strings, plurals, [] (int n) -> int { return plural (n); });
		}
	} // spanish
} // i18n
} // i2p
