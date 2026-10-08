/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Tintin_reporter.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: shilal <shilal@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/08 00:04:04 by shilal            #+#    #+#             */
/*   Updated: 2026/10/08 00:04:04 by shilal           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef TINTIN_REPORTER_HPP
#define TINTIN_REPORTER_HPP

#include <fstream>
#include <string>

class Tintin_reporter {
	private :
		std::ofstream outfile;

		// Streams cannot be shared by copying this reporter.
		Tintin_reporter(const Tintin_reporter &other);
		Tintin_reporter &operator=(const Tintin_reporter &other);

		void write(const std::string &level, const std::string &message);

	public :
		Tintin_reporter();
		~Tintin_reporter();

		void open();
		void info(const std::string &message);
		void input(const std::string &message);
		void error(const std::string &message);
};

#endif
