/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/09 11:18:09 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/CGIHandler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <string.h>

CGIHandler::CGIHandler() {}
CGIHandler::~CGIHandler() {}

Response CGIHandler::execute(const Request& req)
{
	Response res;

	std::cout << "Executing CGI script: " << req.path << std::endl;

	std::map<std::string, std::string> env = buildEnv(req, req.path);

	std::string output = runCGI(req.path, env, req.body);

	res.statusCode = 200;
	res.reasonPhrase = "OK";
	res.headers["Content-Type"] = "text/html";
	res.body = output;
	res.headers["Content-Length"] = std::to_string(res.body.size());

	return res;
}

std::map<std::string, std::string> CGIHandler::buildEnv(const Request& req, const std::string& scriptPath)
{
	std::map<std::string, std::string> env;
	env["GATEWAY_INTERFACE"] = "CGI/1.1";
	env["REQUEST_METHOD"] = req.method;
	env["SCRIPT_FILENAME"] = scriptPath;
	env["SCRIPT_NAME"] = req.path;
	env["QUERY_STRING"] = req.query;
	env["CONTENT_LENGTH"] = std::to_string(req.body.size());
	env["CONTENT_TYPE"] = "text/plain";
	env["SERVER_PROTOCOL"] = "HTTP/1.1";
	env["SERVER_SOFTWARE"] = "webserv/1.0";
	env["REDIRECT_STATUS"] = "200";
	return env;
}

static std::string getInterpreter(const std::string& scriptPath)
{
    if (scriptPath.find(".py") != std::string::npos)
        return "/usr/bin/python3";
    if (scriptPath.find(".php") != std::string::npos)
        return "/usr/local/bin/php-cgi";
    return "";
}

Response CGIHandler::executeWith(const Request& req, const std::string& execPath, const std::string& scriptFile)
{
    Response res;

    std::cout << "Executing CGI executable: " << execPath
              << " (script file: " << scriptFile << ")" << std::endl;

    std::map<std::string, std::string> env = buildEnv(req, scriptFile);

    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        perror("pipe");
        res.statusCode = 500;
        res.reasonPhrase = "Internal Server Error";
        res.body = "<h1>CGI pipe error</h1>";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        res.headers["Content-Type"] = "text/html";
        return res;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        // ========================================
        // CHILD PROZESS
        // ========================================
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        // Extrahiere Verzeichnis und Dateinamen
        std::string scriptDir;
        std::string scriptName;
        
        size_t lastSlash = scriptFile.find_last_of('/');
        if (lastSlash != std::string::npos) {
            scriptDir = scriptFile.substr(0, lastSlash);
            scriptName = scriptFile.substr(lastSlash + 1);  // Nur Dateiname!
        } else {
            scriptDir = ".";
            scriptName = scriptFile;
        }

        // Working Directory setzen
        if (chdir(scriptDir.c_str()) != 0) {
            perror("chdir to script directory");
            exit(1);  // Bei chdir-Fehler abbrechen
        }

        // envp bauen
        char** envp = new char*[env.size() + 1];
        int i = 0;
        for (std::map<std::string, std::string>::const_iterator it = env.begin();
             it != env.end(); ++it)
        {
            std::string entry = it->first + "=" + it->second;
            envp[i++] = strdup(entry.c_str());
        }
        envp[i] = NULL;

        // argv bauen - WICHTIG: Nur den Dateinamen verwenden!
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(execPath.c_str()));
        if (!scriptName.empty())
            argv.push_back(const_cast<char*>(scriptName.c_str()));  // Nur Dateiname!
        argv.push_back(NULL);

        execve(execPath.c_str(), &argv[0], envp);
        
        // execve failed - cleanup
        for (int j = 0; envp[j]; ++j)
            free(envp[j]);
        delete[] envp;
        
        perror("execve");
        exit(1);
    }
    else if (pid > 0)
    {
        // ========================================
        // PARENT PROZESS
        // ========================================
        close(pipeIn[0]);
        close(pipeOut[1]);

        // Request-Body schreiben
        if (!req.body.empty())
        {
            ssize_t written = write(pipeIn[1], req.body.c_str(), req.body.size());
            if (written < 0)
                perror("write to CGI stdin");
        }
        close(pipeIn[1]);

        // Output lesen
        std::ostringstream output;
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
            output.write(buffer, bytes);
        close(pipeOut[0]);

        // Auf Child warten und Exit-Status prüfen
        int status;
        waitpid(pid, &status, 0);

        // Exit-Status auswerten
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            std::cerr << "CGI exited with code " << WEXITSTATUS(status) << std::endl;
            res.statusCode = 500;
            res.reasonPhrase = "Internal Server Error";
            res.body = "<h1>CGI Error</h1><p>Script exited with error code " 
                       + std::to_string(WEXITSTATUS(status)) + "</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }
        if (WIFSIGNALED(status))
        {
            std::cerr << "CGI killed by signal " << WTERMSIG(status) << std::endl;
            res.statusCode = 500;
            res.reasonPhrase = "Internal Server Error";
            res.body = "<h1>CGI Crashed</h1><p>Script killed by signal " 
                       + std::to_string(WTERMSIG(status)) + "</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        // Erfolg
        std::string out = output.str();
        res.statusCode = 200;
        res.reasonPhrase = "OK";
        res.headers["Content-Type"] = "text/html";
        res.body = out;
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    }
    else
    {
        perror("fork");
        res.statusCode = 500;
        res.reasonPhrase = "Internal Server Error";
        res.body = "<h1>CGI fork error</h1>";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        res.headers["Content-Type"] = "text/html";
        return res;
    }
}


std::string CGIHandler::runCGI(const std::string& scriptPath, 
                                const std::map<std::string, std::string>& env, 
                                const std::string& body)
{
    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        perror("pipe");
        return "<h1>CGI pipe error</h1>";
    }

    pid_t pid = fork();

    if (pid == 0)
    {
        // ========================================
        // CHILD PROZESS
        // ========================================
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        // Extrahiere Verzeichnis und Dateinamen
        std::string scriptDir;
        std::string scriptName;
        
        size_t lastSlash = scriptPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            scriptDir = scriptPath.substr(0, lastSlash);
            scriptName = scriptPath.substr(lastSlash + 1);
        } else {
            scriptDir = ".";
            scriptName = scriptPath;
        }

        // Working Directory setzen
        if (chdir(scriptDir.c_str()) != 0) {
            perror("chdir to script directory");
            exit(1);
        }

        // envs bauen
        char** envp = new char*[env.size() + 1];
        int i = 0;
        for (std::map<std::string, std::string>::const_iterator it = env.begin(); 
             it != env.end(); ++it)
        {
            std::string entry = it->first + "=" + it->second;
            envp[i++] = strdup(entry.c_str());
        }
        envp[i] = NULL;

        // argv bauen
        std::string interpreter = getInterpreter(scriptPath);
        char* argv[3];
        
        if (!interpreter.empty()) {
            // z.B. PHP, Python ohne Shebang
            argv[0] = const_cast<char*>(interpreter.c_str());
            argv[1] = const_cast<char*>(scriptName.c_str());  // Nur Dateiname!
            argv[2] = NULL;
            execve(interpreter.c_str(), argv, envp);
        } else {
            // Script hat Shebang
            argv[0] = const_cast<char*>(scriptName.c_str());  // Nur Dateiname!
            argv[1] = NULL;
            execve(scriptName.c_str(), argv, envp);
        }

        // execve failed - cleanup
        for (int j = 0; envp[j]; ++j)
            free(envp[j]);
        delete[] envp;

        perror("execve");
        exit(1);
    }
    else if (pid > 0)
    {
        // ========================================
        // PARENT PROZESS
        // ========================================
        close(pipeIn[0]);
        close(pipeOut[1]);

        // Schreibe Request-Body
        if (!body.empty()) {
            size_t to_write = body.size();
            const char* ptr = body.c_str();
            while (to_write > 0) {
                ssize_t written = write(pipeIn[1], ptr, to_write);
                if (written < 0) {
                    if (errno == EAGAIN || errno == EINTR) continue;
                    perror("write to CGI stdin");
                    close(pipeIn[1]);
                    close(pipeOut[0]);
                    waitpid(pid, NULL, 0);
                    return "<h1>CGI write error</h1>";
                } else if (written == 0) {
                    break;
                }
                ptr += written;
                to_write -= written;
            }
        }
        close(pipeIn[1]);

        // Lese Ausgabe
        std::ostringstream output;
        char buffer[4096];
        while (true) {
            ssize_t bytes = read(pipeOut[0], buffer, sizeof(buffer));
            if (bytes > 0) {
                output.write(buffer, bytes);
            } else if (bytes == 0) {
                break;
            } else {
                if (errno == EAGAIN || errno == EINTR) continue;
                perror("read from CGI stdout");
                break;
            }
        }
        close(pipeOut[0]);

        // Exit-Status prüfen
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            std::cerr << "CGI exited with code " << WEXITSTATUS(status) << std::endl;
            return "<h1>CGI Error</h1><p>Script exited with error code " 
                   + std::to_string(WEXITSTATUS(status)) + "</p>";
        }
        if (WIFSIGNALED(status))
        {
            std::cerr << "CGI killed by signal " << WTERMSIG(status) << std::endl;
            return "<h1>CGI Crashed</h1><p>Script killed by signal " 
                   + std::to_string(WTERMSIG(status)) + "</p>";
        }

        return output.str();
    }
    else
    {
        perror("fork");
        return "<h1>CGI fork error</h1>";
    }
}