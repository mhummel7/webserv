/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/16 11:41:04 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/CGIHandler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <iostream>
#include <string.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <poll.h>

static long long get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

CGIHandler::CGIHandler() {}
CGIHandler::~CGIHandler() {}

Response CGIHandler::execute(const Request& req)
{
    Response res;
    size_t timeout_ms = g_cfg.keepalive_timeout_ms;

#ifdef DEBUG
    std::cout << "Executing CGI script: " << req.path 
              << " (timeout: " << timeout_ms << "ms)" << std::endl;
#endif
    std::map<std::string, std::string> env = buildEnv(req, req.path);
    std::string output = runCGI(req.path, env, req.body, timeout_ms);

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
    if (scriptPath.find(".rb") != std::string::npos)
        return "/usr/bin/ruby";
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

static bool write_with_timeout(int fd, const std::string& data, size_t timeout_ms, long long start_time)
{
    if (data.empty())
        return true;

    size_t written = 0;
    const char* buf = data.c_str();
    size_t total = data.size();

    while (written < total)
    {
        if (get_time_ms() - start_time > static_cast<long long>(timeout_ms))
            return false;

        ssize_t n = write(fd, buf + written, total - written);
        
        if (n > 0)
        {
            written += static_cast<size_t>(n);
        }
        else if (n < 0)
        {
            usleep(1000);
            continue;
        }
        else
        {
            return false;
        }
    }
    return true;
}

static bool read_with_poll_timeout(int fd, std::string& output, pid_t pid, size_t timeout_ms)
{
    long long read_start = get_time_ms();
    bool process_done = false;
    char buffer[4096];

    while (!process_done)
    {
        // Timeout check
        long long elapsed = get_time_ms() - read_start;
        if (elapsed > static_cast<long long>(timeout_ms))
            return false;

        // Check if process exited
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        
        if (ret == pid)
        {
            process_done = true;
            
            // Read any remaining data
            ssize_t bytes;
            while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
            {
                output.append(buffer, static_cast<size_t>(bytes));
            }
            
            // Check exit status
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            {
                std::cerr << "CGI exited with code " << WEXITSTATUS(status) << std::endl;
                return false;
            }
            if (WIFSIGNALED(status))
            {
                std::cerr << "CGI killed by signal " << WTERMSIG(status) << std::endl;
                return false;
            }
            break;
        }
        else if (ret < 0)
        {
            perror("waitpid");
            return false;
        }

        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int remaining_ms = static_cast<int>(timeout_ms - elapsed);
        if (remaining_ms < 0) remaining_ms = 0;

        int poll_ret = poll(&pfd, 1, std::min(remaining_ms, 100));

        if (poll_ret > 0 && (pfd.revents & POLLIN))
        {
            ssize_t bytes = read(fd, buffer, sizeof(buffer));
            if (bytes > 0)
            {
                output.append(buffer, static_cast<size_t>(bytes));
                read_start = get_time_ms(); 
            }
            else if (bytes == 0)
            {
                continue;
            }
        }
        else if (poll_ret < 0)
        {
            perror("poll");
            return false;
        }
    }

    return true;
}

std::string CGIHandler::runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, const std::string& body, size_t timeout_ms)
{
    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        perror("pipe");
        return "<h1>CGI pipe error</h1>";
    }

    fcntl(pipeOut[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeIn[1], F_SETFL, O_NONBLOCK);

    pid_t pid = fork();

    if (pid == 0)
    {
        // CHILD PROCESS
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
        // PARENT PROCESS
        close(pipeIn[0]);
        close(pipeOut[1]);

        long long start_time = get_time_ms();

        bool write_ok = write_with_timeout(pipeIn[1], body, timeout_ms, start_time);
        close(pipeIn[1]);

        if (!write_ok)
        {
            kill(pid, SIGKILL);
            close(pipeOut[0]);
            waitpid(pid, NULL, 0);
            return "<h1>CGI Timeout Error</h1><p>Timeout while writing to script</p>";
        }

        std::string output;
        bool read_ok = read_with_poll_timeout(pipeOut[0], output, pid, timeout_ms);
        
        close(pipeOut[0]);

        if (!read_ok)
        {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            std::cerr << "CGI script timed out after " << timeout_ms << "ms" << std::endl;
            return "<h1>CGI Timeout</h1><p>Script exceeded maximum execution time of " 
                   + std::to_string(timeout_ms / 1000) + " seconds</p>";
        }

        return output;
    }
    else
    {
        perror("fork");
        return "<h1>CGI fork error</h1>";
    }
}

Response CGIHandler::executeWith(const Request& req, const std::string& execPath, const std::string& scriptFile)
{
    Response res;
    size_t timeout_ms = g_cfg.keepalive_timeout_ms;

    std::cout << "Executing CGI executable: " << execPath
              << " (script file: " << scriptFile << ")"
              << " (timeout: " << timeout_ms << "ms)" << std::endl;

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

    fcntl(pipeOut[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeIn[1], F_SETFL, O_NONBLOCK);

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

        long long start_time = get_time_ms();

        bool write_ok = write_with_timeout(pipeIn[1], req.body, timeout_ms, start_time);
        close(pipeIn[1]);

        if (!write_ok)
        {
            kill(pid, SIGKILL);
            close(pipeOut[0]);
            waitpid(pid, NULL, 0);
            
            res.statusCode = 504;
            res.reasonPhrase = "Gateway Timeout";
            res.body = "<h1>504 Gateway Timeout</h1><p>Timeout while writing to CGI script</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        std::string output;
        bool read_ok = read_with_poll_timeout(pipeOut[0], output, pid, timeout_ms);
        
        close(pipeOut[0]);
        
        if (!read_ok)
        {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            std::cerr << "CGI script timed out after " << timeout_ms << "ms" << std::endl;
            
            res.statusCode = 504;
            res.reasonPhrase = "Gateway Timeout";
            res.body = "<h1>504 Gateway Timeout</h1><p>CGI script exceeded maximum execution time of " 
                       + std::to_string(timeout_ms / 1000) + " seconds</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        res.statusCode = 200;
        res.reasonPhrase = "OK";
        res.headers["Content-Type"] = "text/html";
        res.body = output;
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