/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nlewicki <nlewicki@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/27 12:04:43 by nlewicki         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "HTTPHandler.hpp"
#include "Response.hpp"
#include <string>
#include <map>

class CGIHandler
{
public:
	CGIHandler();
	~CGIHandler();

	// Führt das CGI-Script aus und gibt HTTP-Response zurück
	Response execute(const Request& req);
	Response executeWith(const Request& req, const std::string& execPath, const std::string& scriptFile);
private:
	// Hilfsfunktionen
	std::map<std::string, std::string> buildEnv(const Request& req, const std::string& scriptPath);
	std::string runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, const std::string& body);
};

#endif
