/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nlewicki <nlewicki@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:24 by mhummel          #+#    #+#             */
/*   Updated: 2025/11/18 12:36:58 by nlewicki         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */


#include "../include/Response.hpp"
#include "../include/CGIHandler.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <iostream>
#include <iomanip>
#include <dirent.h>
#include <algorithm>
#include <cctype>
#include "../include/config.hpp"  // Für g_cfg (global Config)
#include "../include/Server.hpp"  // Für ServerConfig (falls nicht schon da)


ResponseHandler::ResponseHandler() {}
ResponseHandler::~ResponseHandler() {}

// Response-Object to HTTP-string
std::string Response::toString() const
{
    std::ostringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << reasonPhrase << "\r\n";
	for (size_t i = 0; i < set_cookies.size(); ++i)																// set cookies
        ss << "Set-Cookie: " << set_cookies[i] << "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)	// headers
        ss << it->first << ": " << it->second << "\r\n";
    ss << "\r\n";
    ss << body;
    return ss.str();
}

// Setzt ein Cookie im Response
void Response::setCookie(const std::string& name, const std::string& value, const std::string& path, int maxAge, bool httpOnly,
						 const std::string& sameSite)
{
	std::ostringstream sc;
	sc << name << "=" << value;
	if (maxAge >= 0) sc << "; Max-Age=" << maxAge;
	if (!path.empty()) sc << "; Path=" << path;
	if (httpOnly) sc << "; HttpOnly";
	if (!sameSite.empty()) sc << "; SameSite=" << sameSite; // "Lax"|"Strict"|"None"
	set_cookies.push_back(sc.str());
}

static bool isCGIRequest(const std::string& path)
{
    // Arbeitskopie
    std::string p = path;

    // Entferne CR/LF und führende/trailing whitespace
    while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || isspace((unsigned char)p.back())))
        p.pop_back();
    size_t start = 0;
    while (start < p.size() && isspace((unsigned char)p[start])) ++start;
    if (start) p = p.substr(start);

    // Entferne Query-String / Fragment (teile nach ? oder #)
    size_t q = p.find_first_of("?#");
    if (q != std::string::npos) p.resize(q);

    // Nimm nur letzten Pfad-Element (falls ein voller Pfad übergeben wurde)
    size_t lastSlash = p.find_last_of('/');
    std::string last = (lastSlash == std::string::npos) ? p : p.substr(lastSlash + 1);

    // Finde Extension
    size_t dot = last.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = last.substr(dot + 1);

    // lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return (ext == "py" || ext == "php" || ext == "cgi");
}

// URL-decode (simple)
static std::string urlDecode(const std::string& s) {
    std::string ret;
    ret.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            ret += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            ret += ' ';
        } else {
            ret += s[i];
        }
    }
    return ret;
}

// Normiert Pfad: entfernt doppelte Slashes, einfache Normalisierung
static std::string normalizePath(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    bool lastSlash = false;
    for (char c : path) {
        if (c == '/') {
            if (!lastSlash) { out += '/'; lastSlash = true; }
        } else {
            out += c; lastSlash = false;
        }
    }
    if (out.size() > 1 && out.back() == '/') out.pop_back(); // entferne letzten Slash (außer "/" selbst)
    if (out.empty()) out = "/";
    return out;
}

static bool containsPathTraversal(const std::string& s) {
    if (s.find("..") != std::string::npos) return true;
    return false;
}

// Einfaches join (achtet auf Slashes)
static std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '/') out += '/';
    if (b.front() == '/') out += b.substr(1); else out += b;
    return out;
}

// MIME-Mapping (erweiterbar)
static std::string getMimeType(const std::string& path)
{
    static const std::map<std::string, std::string> m = {
        { "html", "text/html" }, { "htm", "text/html" }, { "css", "text/css" },
        { "js", "application/javascript" }, { "json", "application/json" },
        { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
        { "gif", "image/gif" }, { "svg", "image/svg+xml" }, { "txt", "text/plain" },
        { "pdf", "application/pdf" }, { "ico", "image/x-icon" }
    };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    // lower
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = m.find(ext);
    if (it != m.end()) return it->second;
    return "application/octet-stream";
}

// Generiere einfaches Verzeichnis-Listing (HTML)
static std::string generateDirectoryListing(const std::string& dirPath, const std::string& urlPrefix)
{
    DIR* dp = opendir(dirPath.c_str());
    if (!dp) return "<h1>500 Cannot open directory</h1>";
    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
        << urlPrefix << "</title></head><body>";
    out << "<h1>Index of " << urlPrefix << "</h1><ul>";
    struct dirent* e;
    while ((e = readdir(dp)) != NULL) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        // Escape name? minimal:
        out << "<li><a href=\"" << (urlPrefix.back()=='/'? urlPrefix : urlPrefix + "/") << name << "\">"
            << name << "</a></li>";
    }
    out << "</ul></body></html>";
    closedir(dp);
    return out.str();
}

// check file or dir via stat
static bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::string ResponseHandler::getStatusMessage(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 404: return "Not Found";
		case 405: return "Method not Allowed";
        case 413: return "Payload too large";
		default : return "Unkown";
	}
}

std::string ResponseHandler::loadErrorPage(const std::string& errorPath, const std::string& fallbackHtml) {
    if (errorPath.empty()) {
        return fallbackHtml;
    }
    std::string errorBase = "./root/";  // Fixed base for error pages
    std::string relPath = errorPath;
    if (!relPath.empty() && relPath[0] == '/') relPath = relPath.substr(1);  // Trim leading /
    std::string fullPath = errorBase + relPath;
    if (fileExists(fullPath)) {
        return readFile(fullPath);
    }
    std::cerr << "Warning: Error page not found at " << fullPath << std::endl;
    return fallbackHtml;
}

std::string ResponseHandler::readFile(const std::string& path)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return "<h1>Error opening file</h1>";;

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool ResponseHandler::fileExists(const std::string& path)
{
	struct stat buf;
	return (stat(path.c_str(), &buf) == 0);
}

void setHeaders(Response& res, const Request& req)
{
	res.headers["Server"] = "webserv/1.0";
	res.headers["Connection"] = req.keep_alive ? "keep-alive" : "close";
	res.headers["Keep-Alive"] = req.keep_alive ? "timeout=5, max=100" : "timeout=0, max=0";
    res.headers["Content-Type"] = "text/html";

}

// sanitize/normalize color cookie value, returns empty string on invalid input
static std::string sanitizeColor(const std::string& raw)
{
    if (raw.empty()) return "";
    std::string s = raw;
    // if percent-encoded, decode first
    s = urlDecode(s);
    // allow "#rrggbb" or "rrggbb"
    if (s.size() == 6 && s[0] != '#') s = "#" + s;
    if (s.size() != 7) return "";
    for (size_t i = 1; i < s.size(); ++i) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return "";
    }
    return s;
}

// helper: return color cookie value
static std::string cookieColor(const Request& req)
{
    if (req.cookies.count("color")) return req.cookies.at("color");
    if (req.cookies.count("bg"))    return req.cookies.at("bg");
    return "";
}

// response with given status and body (keeps Content-Type html by default)
Response ResponseHandler::makeHtmlResponse(int status, const std::string& body)
{
    Response r;
    r.statusCode = status;
    r.reasonPhrase = getStatusMessage(status);
    r.body = body;
    r.headers["Content-Type"] = "text/html";
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

// handle directory: index file, autoindex, or forbidden
bool ResponseHandler::handleDirectoryRequest(const std::string& url, const std::string& fsPath,
                                   const LocationConfig& config, Response& res)
{
    std::string indexFile = joinPath(fsPath, config.index.empty() ? "index.html" : config.index);
    if (fileExists(indexFile)) {
        res.statusCode = 200;
        res.reasonPhrase = getStatusMessage(200);
        res.body = readFile(indexFile);
        res.headers["Content-Type"] = getMimeType(indexFile);
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }
    if (config.autoindex) {
        res.statusCode = 200;
        res.reasonPhrase = getStatusMessage(200);
        res.body = generateDirectoryListing(fsPath, url);
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }
    // index disabled
    res = makeHtmlResponse(404, "<h1>404 Not Found</h1>");
    return true;
}

// handle static file or CGI. returns true if handled (res filled), false if not found.
bool ResponseHandler::handleFileOrCgi(const Request& req, const std::string& fsPath,
                            const LocationConfig& config, Response& res)
{
    if (!fileExists(fsPath))
        return false;

    // Extension bestimmen (inklusive Punkt: ".bla")
    std::string ext;
    size_t dot = fsPath.find_last_of('.');
    if (dot != std::string::npos)
        ext = fsPath.substr(dot);

    std::map<std::string, std::string>::const_iterator it = config.cgi.find(ext);
    if (it != config.cgi.end())
    {
        const std::string& execPath = it->second;

        CGIHandler cgi;
        Response r = cgi.executeWith(req, execPath, fsPath);

        r.keep_alive = req.keep_alive;
        if (!r.headers.count("Content-Length"))
            r.headers["Content-Length"] = std::to_string(r.body.size());
        res = r;
        return true;
    }

    if (isCGIRequest(fsPath)) {
        CGIHandler cgi;
        Request req_cgi = req;
        req_cgi.path = fsPath;
        res = cgi.execute(req_cgi);
        res.keep_alive = req.keep_alive;
        if (!res.headers.count("Content-Length"))
            res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }

    res.statusCode = 200;
    res.reasonPhrase = getStatusMessage(200);
    res.body = readFile(fsPath);
    res.headers["Content-Type"] = getMimeType(fsPath);
    res.headers["Content-Length"] = std::to_string(res.body.size());
    return true;
}

static std::string extractValidatedColor(const Request& req)
{
    return sanitizeColor(cookieColor(req));
}

Response& ResponseHandler::methodGET(const Request& req, Response& res, const LocationConfig& config, const ServerConfig& serverConfig) {
    std::string url = urlDecode(req.path);
    if (url.empty()) url = "/";
    url = normalizePath(url);

    // security check
    if (containsPathTraversal(url)) {
        res.statusCode = 403;
        res.reasonPhrase = getStatusMessage(403);
        std::string fallback = "<h1>403 Forbidden</h1>";
        std::string errorPath;
        if (config.error_pages.count(403)) {
            errorPath = config.error_pages.at(403);
        } else if (serverConfig.error_pages.count(403)) {
            errorPath = serverConfig.error_pages.at(403);
        } else if (g_cfg.default_error_pages.count(403)) {
            errorPath = g_cfg.default_error_pages.at(403);
        }
        res.body = loadErrorPage(errorPath, fallback);
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    }

    // prepare filesystem path relative to location root
    std::string fsPath = config.root.empty() ? std::string(".") : config.root;
    std::string trimmedUrl = url;
    if (!config.path.empty() && config.path != "/" && trimmedUrl.find(config.path) == 0) {
        trimmedUrl = trimmedUrl.substr(config.path.length());
        if (trimmedUrl.empty()) trimmedUrl = "/";
    }
    fsPath = joinPath(fsPath, trimmedUrl);

    std::string color = extractValidatedColor(req);

    if (isDirectory(fsPath)) {
        handleDirectoryRequest(url, fsPath, config, res);
        return res;
    }

    if (handleFileOrCgi(req, fsPath, config, res)) {
        if (res.headers["Content-Type"] == "text/html") {
            size_t pos = res.body.find("<body");
            if (pos != std::string::npos) {
                size_t end = res.body.find(">", pos);
                if (end != std::string::npos) {
                    std::string insert = " style=\"--user-color: " +
                        (color.empty() ? std::string("#ffffff") : color) + ";\"";
                    res.body.insert(end, insert);
                    res.headers["Content-Length"] = std::to_string(res.body.size());
                }
            }
        }
        return res;
    }

    // not found – 404-BLOCK
    res.statusCode = 404;
    res.reasonPhrase = getStatusMessage(404);
    std::string errorPath;
    if (config.error_pages.count(404)) {
        errorPath = config.error_pages.at(404);
    } else if (serverConfig.error_pages.count(404)) {
        errorPath = serverConfig.error_pages.at(404);
    } else if (g_cfg.default_error_pages.count(404)) {
        errorPath = g_cfg.default_error_pages.at(404);
    }

    std::string fallback = "<h1>404 Not Found</h1>";
    res.body = loadErrorPage(errorPath, fallback);
    res.headers["Content-Type"] = "text/html";
    res.headers["Content-Length"] = std::to_string(res.body.size());
    return res;
}

Response& ResponseHandler::methodPOST(const Request& req, Response& res, const LocationConfig& config)
{
    std::string url = urlDecode(req.path);
    if (url.empty()) url = "/";
    url = normalizePath(url);

    if (containsPathTraversal(url)) {
        res = makeHtmlResponse(403, "<h1>403 Forbidden</h1>");
        return res;
    }

    std::string fsPath = config.root.empty() ? std::string(".") : config.root;
    std::string trimmedUrl = url;
    if (!config.path.empty() && config.path != "/" && trimmedUrl.find(config.path) == 0) {
        trimmedUrl = trimmedUrl.substr(config.path.length());
        if (trimmedUrl.empty()) trimmedUrl = "/";
    }
    fsPath = joinPath(fsPath, trimmedUrl);

    std::string ext;
    size_t dot = fsPath.find_last_of('.');
    if (dot != std::string::npos)
        ext = fsPath.substr(dot);

    // CGI-Mapping check
    std::map<std::string, std::string>::const_iterator it = config.cgi.find(ext);
    if (it != config.cgi.end())
    {
        const std::string& execPath = it->second;
        CGIHandler cgi;
        Response r = cgi.executeWith(req, execPath, fsPath);
        r.keep_alive = req.keep_alive;
        if (!r.headers.count("Content-Length"))
            r.headers["Content-Length"] = std::to_string(r.body.size());
        res = r;
        return res;
    }

    std::string dir = config.root;
    if (dir.empty())
        dir = "./root/data/";

#ifdef DEBUG
	std::cout << "POST data dir: " << dir << std::endl;
#endif
	std::string contentType;
	if (req.headers.count("Content-Type"))
		contentType = req.headers.find("Content-Type")->second;

    // Multipart-Formular-Upload
	if (contentType.find("multipart/form-data") != std::string::npos)
	{
		size_t pos = contentType.find("boundary=");
		if (pos == std::string::npos)
		{
			res.statusCode = 400;
			res.reasonPhrase = "Bad Request";
			res.body = "<h1>400 Bad Request</h1><p>Missing multipart boundary.</p>";
		}
		else
		{
			std::string boundary = "--" + contentType.substr(pos + 9);
			std::string body = req.body;

			size_t fileStart = body.find("filename=\"");
			if (fileStart == std::string::npos)
			{
				res.statusCode = 400;
				res.reasonPhrase = "Bad Request";
				res.body = "<h1>400 Bad Request</h1><p>No file field found.</p>";
			}
			else
			{
				fileStart += 10;
				size_t fileEnd = body.find("\"", fileStart);
				std::string originalName = body.substr(fileStart, fileEnd - fileStart);

				size_t dataStart = body.find("\r\n\r\n", fileEnd);
				if (dataStart == std::string::npos)
				{
					res.statusCode = 400;
					res.reasonPhrase = "Bad Request";
					res.body = "<h1>400 Bad Request</h1><p>Malformed multipart data.</p>";
				}
				else
				{
					dataStart += 4;
					size_t dataEnd = body.find(boundary, dataStart);
					std::string fileContent = body.substr(dataStart, dataEnd - dataStart);

					if (fileContent.size() >= 2 && fileContent[fileContent.size() - 2] == '\r')
						fileContent.erase(fileContent.size() - 2);

					for (size_t i = 0; i < originalName.size(); ++i)
						if (originalName[i] == '/' || originalName[i] == '\\')
							originalName[i] = '_';

					std::string filePath = dir + "/" + originalName;

					std::ofstream out(filePath.c_str(), std::ios::binary);
					if (!out.is_open())
					{
						res.statusCode = 500;
						res.reasonPhrase = "Internal Server Error";
						res.body = "<h1>500 Internal Server Error</h1><p>Could not write to data folder.</p>";
					}
					else
					{
						out.write(fileContent.data(), fileContent.size());
						out.close();

						res.statusCode = 200;
						res.reasonPhrase = getStatusMessage(200);
						res.body = "<h1>Upload successful!</h1><p>Saved as " + filePath + "</p>";
					}
				}
			}
		}
	}

	// Fallback: raw upload
	else
	{
		std::string filename = dir + "/upload_" + std::to_string(time(NULL));
		std::ofstream out(filename.c_str(), std::ios::binary);
		if (!out.is_open())
		{
			res.statusCode = 500;
			res.reasonPhrase = "Internal Server Error";
			res.body = "<h1>500 Internal Server Error</h1><p>Could not write to data folder.</p>";
		}
		else
		{
			out.write(req.body.data(), req.body.size());
			out.close();

			res.statusCode = 200;
			res.reasonPhrase = getStatusMessage(200);
			res.body = "<h1>POST stored successfully!</h1><p>Saved as " + filename + "</p>";
		}
	}
	return res;
}

Response& ResponseHandler::methodDELETE(const Request& req, Response& res, const LocationConfig& config)
{
    #ifdef DEBUG
    std::cout << "[DELETE] Request body: '" << req.body << "'" << std::endl;
    std::cout << "[DELETE] Request path: " << req.path << std::endl;
    std::cout << "[DELETE] Config root: " << config.root << std::endl;
    #endif

    // 1. Dateiname aus dem Body extrahieren (Whitespace entfernen!)
    std::string filename = req.body;

    // Entferne Newlines und Whitespace vom Ende
    while (!filename.empty() && (filename.back() == '\r' || filename.back() == '\n' ||
           filename.back() == ' ' || filename.back() == '\t')) {
        filename.pop_back();
    }

    // 2. Sicherheitsprüfung
    if (filename.empty()) {
        res = makeHtmlResponse(400, "<h1>400 Bad Request - No filename specified</h1>");
        return res;
    }

    if (containsPathTraversal(filename) || filename.find('/') != std::string::npos) {
        res = makeHtmlResponse(403, "<h1>403 Forbidden - Invalid filename</h1>");
        return res;
    }

    // 3. Dateipfad erstellen
    std::string baseDir;
    if (!config.data_dir.empty()) {
        baseDir = config.data_dir;
    } else if (!config.root.empty()) {
        baseDir = config.root;
    } else {
        baseDir = "./root/data";
    }

    // Sicherstellen, dass baseDir mit / endet
    if (!baseDir.empty() && baseDir.back() != '/') {
        baseDir += "/";
    }

    std::string filepath = baseDir + filename;

    #ifdef DEBUG
    std::cout << "[DELETE] Full path: " << filepath << std::endl;
    #endif

    if (fileExists(filepath) && std::remove(filepath.c_str()) == 0) {
        res.statusCode = 200;
        res.reasonPhrase = getStatusMessage(200);
        res.body = "<h1>File '" + filename + "' deleted successfully.</h1>";
    } else {
        res.statusCode = 404;
        res.reasonPhrase = getStatusMessage(404);
        res.body = "<h1>404 File '" + filename + "' not found.</h1>";
    }
    return res;
}

Response ResponseHandler::handleRequest(const Request& req, const LocationConfig& locConfig, const ServerConfig& serverConfig)
{
   if (req.error != 0)
   {
    // std::cout << "req.error nicht 0:" << req.error << std::endl;
    Response res;
    res.statusCode = req.error;
    res.reasonPhrase = getStatusMessage(req.error);
    res.body = "<h1>413 Payload too large.</h1>";

    std::string errorPath;
    if (locConfig.error_pages.count(req.error)) {
        errorPath = locConfig.error_pages.at(req.error);
    } else if (serverConfig.error_pages.count(req.error)) {
        errorPath = serverConfig.error_pages.at(req.error);
    } else if (g_cfg.default_error_pages.count(req.error)) {
        errorPath = g_cfg.default_error_pages.at(req.error);
    }

    std::string fallback = "<h1>" + std::to_string(req.error) + " " + res.reasonPhrase + "</h1>";
    res.body = loadErrorPage(errorPath, fallback);

    res.headers["Content-Type"] = "text/html";
    res.headers["Content-Length"] = std::to_string(res.body.size());
    res.keep_alive = false;
    // std::cout << "ende von decode error res.status:" << res.statusCode << std::endl;
    return res;
    }

    Response res;
    res.keep_alive = req.keep_alive;

    std::string fullPath = locConfig.root;
    std::string decodedPath = urlDecode(req.path);
    if (decodedPath.empty() || decodedPath == "/") {
        fullPath += "/" + (locConfig.index.empty() ? "index.html" : locConfig.index);
    } else {
        fullPath += decodedPath;
    }

    // Default-Headers
    setHeaders(res, req);

#ifdef DEBUG
    printf("Full path: %s\n", fullPath.c_str());
    for (size_t i = 0; i < locConfig.methods.size(); ++i)
        std::cout << locConfig.methods[i] << " ";
    std::cout << std::endl;
#endif

    auto methodIt = std::find(locConfig.methods.begin(), locConfig.methods.end(), req.method);
    if (req.method == "GET" && methodIt != locConfig.methods.end()) {
        return methodGET(req, res, locConfig, serverConfig);
    } else if (req.method == "POST" && methodIt != locConfig.methods.end()) {
        return methodPOST(req, res, locConfig);
    } else if (req.method == "DELETE" && methodIt != locConfig.methods.end()) {
        return methodDELETE(req, res, locConfig);
    } else {
        res.statusCode = 405;
        res.reasonPhrase = getStatusMessage(405);
        res.body = "<h1>405 Method Not Allowed</h1>";
        res.headers["Content-Type"] = "text/html";
    }

    res.headers["Content-Length"] = std::to_string(res.body.size());

#ifdef DEBUG
    std::cout << "method : " << req.method << std::endl;
    std::cout << "path : " << fullPath << std::endl;  // fullPath statt path
    std::cout << "body : " << req.body << std::endl;
    std::cout << res.toString() << std::endl;
#endif

    return res;
}
