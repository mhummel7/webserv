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
#include "../include/config.hpp"
#include "../include/Server.hpp"


ResponseHandler::ResponseHandler() {}
ResponseHandler::~ResponseHandler() {}

// Response-Object to HTTP-string
std::string Response::toString() const
{
    std::ostringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << reasonPhrase << "\r\n";
	for (size_t i = 0; i < set_cookies.size(); ++i)
        ss << "Set-Cookie: " << set_cookies[i] << "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
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
	if (!sameSite.empty()) sc << "; SameSite=" << sameSite;
	set_cookies.push_back(sc.str());
}

static bool isCGIRequest(const std::string& path)
{
    std::string p = path;

    while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || isspace((unsigned char)p.back())))
        p.pop_back();
    size_t start = 0;
    while (start < p.size() && isspace((unsigned char)p[start])) ++start;
    if (start) p = p.substr(start);

    size_t q = p.find_first_of("?#");
    if (q != std::string::npos) p.resize(q);

    size_t lastSlash = p.find_last_of('/');
    std::string last = (lastSlash == std::string::npos) ? p : p.substr(lastSlash + 1);

    size_t dot = last.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = last.substr(dot + 1);

    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return (ext == "py" || ext == "php" || ext == "cgi");
}

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
    if (out.size() > 1 && out.back() == '/') out.pop_back();
    if (out.empty()) out = "/";
    return out;
}

static bool containsPathTraversal(const std::string& s) {
    if (s.find("..") != std::string::npos) return true;
    return false;
}

static std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '/') out += '/';
    if (b.front() == '/') out += b.substr(1); else out += b;
    return out;
}

// MIME-Mapping
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

static std::string htmlEscape(const std::string& str)
{
    std::string result;
    result.reserve(str.size() * 1.2);
    
    for (size_t i = 0; i < str.size(); ++i)
    {
        switch (str[i])
        {
            case '&':  result += "&amp;";   break;
            case '<':  result += "&lt;";    break;
            case '>':  result += "&gt;";    break;
            case '"':  result += "&quot;";  break;
            case '\'': result += "&#39;";   break;
            default:   result += str[i];    break;
        }
    }
    return result;
}

static std::string urlEncode(const std::string& s)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = s[i];
        
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped << c;
        }
        else if (c == ' ')
        {
            escaped << '+';
        }
        else
        {
            escaped << '%' << std::setw(2) << int(c);
        }
    }
    
    return escaped.str();
}

static std::string generateDirectoryListing(const std::string& dirPath, const std::string& urlPrefix)
{
    DIR* dp = opendir(dirPath.c_str());
    if (!dp) return "<h1>500 Cannot open directory</h1>";
    
    std::string escapedPrefix = htmlEscape(urlPrefix);
    
    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\">"
        << "<title>Index of " << escapedPrefix << "</title>"
        << "<style>"
        << "body { font-family: Arial, sans-serif; margin: 20px; }"
        << "h1 { color: #333; }"
        << "ul { list-style: none; padding: 0; }"
        << "li { padding: 5px 0; }"
        << "a { text-decoration: none; color: #0066cc; }"
        << "a:hover { text-decoration: underline; }"
        << "</style>"
        << "</head><body>";
    
    out << "<h1>Index of " << escapedPrefix << "</h1><ul>";
    
    if (urlPrefix != "/")
    {
        std::string parentPath = urlPrefix;
        size_t lastSlash = parentPath.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash > 0)
            parentPath = parentPath.substr(0, lastSlash);
        else
            parentPath = "/";
        
        out << "<li><a href=\"" << htmlEscape(parentPath) << "\">..</a></li>";
    }
    
    struct dirent* e;
    std::vector<std::string> entries;
    
    // Collect all entries first for sorting
    while ((e = readdir(dp)) != NULL)
    {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        entries.push_back(name);
    }
    closedir(dp);
    
    // Sort entries
    std::sort(entries.begin(), entries.end());
    
    // Generate HTML for each entry
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const std::string& name = entries[i];
        
        std::string itemUrl = urlPrefix;
        if (itemUrl.back() != '/') itemUrl += '/';
        itemUrl += urlEncode(name);
        
        // Check if it's a directory
        std::string fullPath = dirPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;
        
        bool isDir = false;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0)
            isDir = S_ISDIR(st.st_mode);
        
        std::string displayName = htmlEscape(name);
        if (isDir) displayName += "/";
        
        out << "<li><a href=\"" << htmlEscape(itemUrl) << "\">" 
            << displayName << "</a></li>";
    }
    
    out << "</ul></body></html>";
    return out.str();
}

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

std::string ResponseHandler::loadErrorPage(const std::string& errorPath, const std::string& fallbackHtml)
{
    if (errorPath.empty())
        return fallbackHtml;

    if (fileExists(errorPath)) {
        return readFile(errorPath);
    }
    std::cerr << "Warning: Error page not found at " << errorPath << std::endl;
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
    s = urlDecode(s);
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
    if (req.cookies.count("color"))
        return req.cookies.at("color");
    if (req.cookies.count("bg"))
        return req.cookies.at("bg");
    return "";
}

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
    res = makeHtmlResponse(404, "<h1>404 Not Found</h1>");
    return true;
}


bool ResponseHandler::handleFileOrCgi(const Request& req, const std::string& fsPath, const LocationConfig& config, Response& res)
{
    if (!fileExists(fsPath))
        return false;

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

        r.keep_alive = false;
        setHeaders(r, req);
        r.headers["Connection"] = "close";
        r.headers["Keep-Alive"] = "timeout=0, max=0";

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
        res.keep_alive = false;
        res.headers["Connection"] = "close";
        res.headers["Keep-Alive"] = "timeout=0, max=0";
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
        if (config.error_pages.count(403))
        {
            errorPath = config.error_pages.at(403);
        }
        else if (serverConfig.error_pages.count(403))
        {
            errorPath = serverConfig.error_pages.at(403);
        }
        else if (g_cfg.default_error_pages.count(403))
        {
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

    std::map<std::string, std::string>::const_iterator it = config.cgi.find(ext);
    if (it != config.cgi.end())
    {
        const std::string& execPath = it->second;

        CGIHandler cgi;
        Response r = cgi.executeWith(req, execPath, fsPath);

        r.keep_alive = false;
        setHeaders(r, req);
        r.headers["Connection"] = "close";
        r.headers["Keep-Alive"] = "timeout=0, max=0";

        if (!r.headers.count("Content-Length"))
            r.headers["Content-Length"] = std::to_string(r.body.size());

        res = r;
        return res;
    }

    if (isCGIRequest(fsPath))
    {
        CGIHandler cgi;
        Request req_cgi = req;
        req_cgi.path = fsPath;
        res = cgi.execute(req_cgi);
        res.keep_alive = false;
        res.headers["Connection"] = "close";
        res.headers["Keep-Alive"] = "timeout=0, max=0";
        if (!res.headers.count("Content-Length"))
            res.headers["Content-Length"] = std::to_string(res.body.size());
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

    std::string filename = req.body;

    while (!filename.empty() && (filename.back() == '\r' || filename.back() == '\n' ||
           filename.back() == ' ' || filename.back() == '\t'))
           {
        filename.pop_back();
    }
    
    size_t start = 0;
    while (start < filename.size() && (filename[start] == ' ' || filename[start] == '\t'))
        ++start;
    if (start > 0)
        filename = filename.substr(start);

    filename = urlDecode(filename);

    if (filename.empty()) {
        res = makeHtmlResponse(400, "<h1>400 Bad Request - No filename specified</h1>");
        return res;
    }

    // Check for path traversal patterns
    if (containsPathTraversal(filename) || 
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos)
    {
        res = makeHtmlResponse(403, "<h1>403 Forbidden - Invalid filename</h1>");
        return res;
    }
    
    if (filename.find('\0') != std::string::npos)
    {
        res = makeHtmlResponse(403, "<h1>403 Forbidden - Invalid filename</h1>");
        return res;
    }

    // Only allow alphanumeric, dash, underscore, dot
    for (size_t i = 0; i < filename.size(); ++i)
    {
        char c = filename[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.')
        {
            res = makeHtmlResponse(403, 
                "<h1>403 Forbidden - Filename contains invalid characters</h1>");
            return res;
        }
    }

    // Prevent hidden files
    if (!filename.empty() && filename[0] == '.')
    {
        res = makeHtmlResponse(403, "<h1>403 Forbidden - Cannot delete hidden files</h1>");
        return res;
    }

    // create path
    std::string baseDir;
    if (!config.data_dir.empty()) {
        baseDir = config.data_dir;
    } else if (!config.root.empty()) {
        baseDir = config.root;
    } else {
        baseDir = "./root/data";
    }

    if (!baseDir.empty() && baseDir.back() != '/') {
        baseDir += "/";
    }

    std::string filepath = baseDir + filename;

    #ifdef DEBUG
    std::cout << "[DELETE] Full path: " << filepath << std::endl;
    #endif

    char resolvedPath[PATH_MAX];
    char resolvedBase[PATH_MAX];
    
    if (realpath(baseDir.c_str(), resolvedBase) == NULL)
    {
        res = makeHtmlResponse(500, "<h1>500 Internal Server Error</h1>");
        return res;
    }
    
    // check if it exists
    if (!fileExists(filepath))
    {
        res.statusCode = 404;
        res.reasonPhrase = getStatusMessage(404);
        res.body = "<h1>404 File '" + htmlEscape(filename) + "' not found.</h1>";
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    }
    
    // Now resolve the file path
    if (realpath(filepath.c_str(), resolvedPath) == NULL)
    {
        res = makeHtmlResponse(403, "<h1>403 Forbidden</h1>");
        return res;
    }
    
    // Check if resolved file path starts with resolved base dir
    std::string resolvedFileStr(resolvedPath);
    std::string resolvedBaseStr(resolvedBase);
    
    if (resolvedFileStr.compare(0, resolvedBaseStr.length(), resolvedBaseStr) != 0)
    {
        res = makeHtmlResponse(403, 
            "<h1>403 Forbidden - File is outside allowed directory</h1>");
        return res;
    }

    if (std::remove(filepath.c_str()) == 0)
    {
        res.statusCode = 200;
        res.reasonPhrase = getStatusMessage(200);
        res.body = "<h1>File '" + htmlEscape(filename) + "' deleted successfully.</h1>";
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
    }
    else
    {
        res.statusCode = 500;
        res.reasonPhrase = "Internal Server Error";
        res.body = "<h1>500 Internal Server Error - Failed to delete file</h1>";
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
    }
    
    return res;
}

Response ResponseHandler::handleRequest(const Request& req, const LocationConfig& locConfig, const ServerConfig& serverConfig)
{
   if (req.error != 0)
   {
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
