/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/12 12:27:57 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/CGIHandler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <string.h>
#include <vector>
#include <memory>
#include <algorithm>

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


static bool chdir_to_script_dir(const std::string& scriptPath, std::string& outDir, std::string& outName)
{
    size_t lastSlash = scriptPath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        outDir = scriptPath.substr(0, lastSlash);
        outName = scriptPath.substr(lastSlash + 1);
    } else {
        outDir = ".";
        outName = scriptPath;
    }
    if (chdir(outDir.c_str()) != 0) {
        perror("chdir to script directory");
        return false;
    }
    return true;
}

static std::vector<char*> build_envp_vec(const std::map<std::string, std::string>& env)
{
    std::vector<char*> envp_vec;
    envp_vec.reserve(env.size() + 1);
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
    {
        std::string entry = it->first + "=" + it->second;
        envp_vec.push_back(strdup(entry.c_str()));
    }
    envp_vec.push_back(NULL);
    return envp_vec;
}

static void free_envp_vec(std::vector<char*>& envp_vec)
{
    for (size_t j = 0; j + 1 < envp_vec.size(); ++j) {
        if (envp_vec[j]) free(envp_vec[j]);
    }
}

static bool write_full(int fd, const std::string& body)
{
    if (body.empty()) return true;
    size_t to_write = body.size();
    const char* ptr = body.c_str();
    while (to_write > 0) {
        ssize_t written = write(fd, ptr, to_write);
        if (written < 0) return false;
        if (written == 0) break;
        ptr += written;
        to_write -= written;
    }
    return true;
}

static std::string read_all(int fd)
{
    std::ostringstream output;
    char buffer[4096];
    while (true) {
        ssize_t bytes = read(fd, buffer, sizeof(buffer));
        if (bytes > 0) {
            output.write(buffer, bytes);
        } else if (bytes == 0) {
            break;
        } else {
            perror("read from CGI stdout");
            break;
        }
    }
    return output.str();
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
        // CHILD
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        std::string scriptDir;
        std::string scriptName;
        if (!chdir_to_script_dir(scriptFile, scriptDir, scriptName))
            exit(1);

        std::vector<char*> envp_vec = build_envp_vec(env);

        std::vector<char*> argv;
        argv.reserve(3);
        argv.push_back(const_cast<char*>(execPath.c_str()));
        if (!scriptName.empty())
            argv.push_back(const_cast<char*>(scriptName.c_str()));
        argv.push_back(NULL);

        execve(execPath.c_str(), argv.data(), envp_vec.data());

        free_envp_vec(envp_vec);
        perror("execve");
        exit(127);
    }
    else if (pid > 0)
    {
        // PARENT PROCESS
        close(pipeIn[0]);
        close(pipeOut[1]);

        if (!write_full(pipeIn[1], req.body)) {
            std::cerr << "Warning: failed to write request body to CGI stdin\n";
        }
        close(pipeIn[1]);
 
        std::string out = read_all(pipeOut[0]);
        close(pipeOut[0]);

        int status;
        waitpid(pid, &status, 0);

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
        // CHILD
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        std::string scriptDir;
        std::string scriptName;
        if (!chdir_to_script_dir(scriptPath, scriptDir, scriptName))
            exit(1);

        std::vector<char*> envp_vec = build_envp_vec(env);

        std::string interpreter = getInterpreter(scriptPath);
        std::vector<char*> argv;
        argv.reserve(3);

        if (!interpreter.empty()) {
            argv.push_back(const_cast<char*>(interpreter.c_str()));
            argv.push_back(const_cast<char*>(scriptName.c_str()));
            argv.push_back(NULL);
            execve(interpreter.c_str(), argv.data(), envp_vec.data());
        } 
        else
        {
            argv.push_back(const_cast<char*>(scriptName.c_str()));
            argv.push_back(NULL);
            execve(scriptName.c_str(), argv.data(), envp_vec.data());
        }

        free_envp_vec(envp_vec);
        perror("execve");
        exit(127);
    }
    else if (pid > 0)
    {
        // PARENT
        close(pipeIn[0]);
        close(pipeOut[1]);

        if (!body.empty())
            write_full(pipeIn[1], body);
        close(pipeIn[1]);

        std::string output = read_all(pipeOut[0]);
        close(pipeOut[0]);

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

        return output;
    }
    else
    {
        perror("fork");
        return "<h1>CGI fork error</h1>";
    }
}