/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/01 16:16:51 by mhummel          ###   ########.fr       */
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

	// baue Umgebungsvariablen
	std::map<std::string, std::string> env = buildEnv(req, req.path);

	// führe Script aus
	std::string output = runCGI(req.path, env, req.body);

	// baue HTTP-Response
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
	env["REDIRECT_STATUS"] = "200";  // notwendig für PHP-CGI
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

Response CGIHandler::executeWith(const Request& req,
                                 const std::string& execPath,
                                 const std::string& scriptFile)
{
    Response res;

    std::cout << "Executing CGI executable: " << execPath
              << " (script file: " << scriptFile << ")" << std::endl;

    // env: SCRIPT_FILENAME = scriptFile
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
        // Child
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

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

        // argv bauen:
        //   - wenn scriptFile nicht leer: execPath scriptFile
        //   - sonst nur execPath
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(execPath.c_str()));
        if (!scriptFile.empty())
            argv.push_back(const_cast<char*>(scriptFile.c_str()));
        argv.push_back(NULL);

        execve(execPath.c_str(), &argv[0], envp);
        perror("execve");
        exit(1);
    }
    else if (pid > 0)
    {
        // Parent
        close(pipeIn[0]);
        close(pipeOut[1]);

        if (!req.body.empty())
        {
            ssize_t written = write(pipeIn[1], req.body.c_str(), req.body.size());
            if (written < 0)
                perror("write to CGI stdin");
        }
        close(pipeIn[1]);

        std::ostringstream output;
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
            output.write(buffer, bytes);
        close(pipeOut[0]);

        waitpid(pid, NULL, 0);

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


std::string CGIHandler::runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, const std::string& body)
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
		dup2(pipeIn[0], STDIN_FILENO);
		dup2(pipeOut[1], STDOUT_FILENO);
		close(pipeIn[1]);
		close(pipeOut[0]);

		// envs bauen
		char** envp = new char*[env.size() + 1];
		int i = 0;
		for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
		{
			std::string entry = it->first + "=" + it->second;
			envp[i++] = strdup(entry.c_str());
		}
		envp[i] = NULL;

		// argv bauen
		std::string interpreter = getInterpreter(scriptPath);
		char* argv[3];
		if (!interpreter.empty()) {
			// z. B. PHP, Python ohne Shebang
			argv[0] = const_cast<char*>(interpreter.c_str());
			argv[1] = const_cast<char*>(scriptPath.c_str());
			argv[2] = NULL;
			execve(interpreter.c_str(), argv, envp);
		} else {
			// Script hat Shebang (#!/usr/bin/python3)
			argv[0] = const_cast<char*>(scriptPath.c_str());
			argv[1] = NULL;
			execve(scriptPath.c_str(), argv, envp);
		}

		perror("execve");
		exit(1);

	}
	else if (pid > 0)
	{
		close(pipeIn[0]);
		close(pipeOut[1]);

		// Schreibe Request-Body an CGI (full write loop)
		if (!body.empty()) {
			size_t to_write = body.size();
			const char* ptr = body.c_str();
			while (to_write > 0) {
				ssize_t written = write(pipeIn[1], ptr, to_write);
				if (written < 0) {
					if (errno == EAGAIN || errno == EINTR) continue;  // Retry
					perror("write to CGI stdin");
					close(pipeIn[1]);
					return "<h1>CGI write error</h1>";
				} else if (written == 0) {
					// EOF – unlikely
					break;
				}
				ptr += written;
				to_write -= written;
			}
		}
		close(pipeIn[1]);

		// Lese Ausgabe vom CGI (loop bis EOF)
		std::ostringstream output;
		char buffer[4096];
		while (true) {
			ssize_t bytes = read(pipeOut[0], buffer, sizeof(buffer));
			if (bytes > 0) {
				output.write(buffer, bytes);
			} else if (bytes == 0) {
				break;  // EOF
			} else {  // bytes < 0
				if (errno == EAGAIN || errno == EINTR) continue;
				perror("read from CGI stdout");
				break;
			}
		}
		close(pipeOut[0]);

		waitpid(pid, NULL, 0);
		return output.str();
	}
	else
	{
		perror("fork");
		return "<h1>CGI fork error</h1>";
	}
}
