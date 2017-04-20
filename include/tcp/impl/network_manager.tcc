
///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                               //
// Copyright 2017 Lucas Lazare.                                                                  //
// This file is part of Breep project which is released under the                                //
// European Union Public License v1.1. If a copy of the EUPL was                                 //
// not distributed with this software, you can obtain one at :                                   //
// https://joinup.ec.europa.eu/community/eupl/og_page/european-union-public-licence-eupl-v11     //
//                                                                                               //
///////////////////////////////////////////////////////////////////////////////////////////////////


#include "tcp/network_manager.hpp" // TODO: remove [Seems useless, but allows my IDE to work]

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp> // boost::uuids::to_string
#include <boost/array.hpp>
#include <boost/uuid/string_generator.hpp>
#include <vector>
#include <limits>
#include <memory>
#include <string>

#include "detail/utils.hpp"
#include "network.hpp"

template <unsigned int T>
template <typename data_container>
inline void breep::tcp::network_manager<T>::send(commands command, const data_container& data, const peernm& peer) const {
	send(command, data.cbegin(), data.size(), peer);
}

template <unsigned int T>
template <typename data_iterator, typename size_type>
void breep::tcp::network_manager<T>::send(commands command, data_iterator it, size_type size, const peernm& peer) const {
	std::vector<uint8_t> buff;
	buff.reserve(2 + size + size / std::numeric_limits<uint8_t>::max());

	buff.push_back(static_cast<uint8_t>(command));
	size_type current_index{0};
	while (current_index < size) {
		if (size - current_index > std::numeric_limits<uint8_t>::max()) {
			buff.push_back(0);
			for (uint8_t i = std::numeric_limits<uint8_t>::max() ; i-- ; ++current_index) {
				buff.push_back(*it++);
			}
		} else {
			buff.push_back(static_cast<uint8_t>(size - current_index));
			while(current_index++ < size) {
				buff.push_back(*it++);
			}
		}
	}
	boost::asio::write(*peer.m_socket, boost::asio::buffer(buff));
}

template <unsigned int T>
breep::peer<breep::tcp::network_manager<T>> breep::tcp::network_manager<T>::connect(const boost::asio::ip::address& address, unsigned short port) {
	boost::asio::ip::tcp::resolver resolver(m_io_service);
	boost::asio::ip::tcp::resolver::iterator endpoint_iterator =
			resolver.resolve(boost::asio::ip::tcp::resolver::query(address.to_string(),std::to_string(port)));

	std::shared_ptr<boost::asio::ip::tcp::socket> socket = std::make_shared<boost::asio::ip::tcp::socket>(m_io_service);
	boost::asio::connect(*socket, endpoint_iterator);
	boost::asio::write(
			*socket,
			boost::asio::buffer(breep::detail::to_bigendian1(boost::uuids::to_string(m_owner->self().id())))
	);
	boost::system::error_code error;
	boost::array<char, 512> buffer;
	size_t len = socket->read_some(boost::asio::buffer(buffer), error);

	if (error) {
		return constant::bad_peer<network_manager<T>>;
	}

	std::vector<char> input{};
	input.reserve(len);
	std::copy(buffer.cbegin(), buffer.cend(), std::back_inserter(input));
	return peernm(
			boost::uuids::string_generator{}(breep::detail::to_bigendian2<std::string>(input)),
			boost::asio::ip::address(address),
			std::move(socket)
	);
}

template <unsigned int T>
void breep::tcp::network_manager<T>::process_connected_peer(peernm& peer) {
	boost::asio::async_read(
			*peer.m_socket,
			boost::asio::buffer(peer.m_fixed_buffer.data(), peer.m_fixed_buffer.size()),
			boost::bind(&network_manager<T>::process_read, this, peer, _1, _2)
	);
}

template <unsigned int T>
inline void breep::tcp::network_manager<T>::disconnect(peernm& peer) {
	send(commands::peer_disconnection, breep::detail::to_bigendian1(boost::uuids::to_string(peer.id())), peer);
	peer.m_socket = std::shared_ptr<socket_type>(nullptr);
}

template <unsigned int T>
inline void breep::tcp::network_manager<T>::owner(network<network_manager<T>>* owner) {
	if (m_owner == nullptr) {
		m_owner = owner;
	} else {
		throw invalid_state("Tried to set an already set owner. This object shouldn't be shared.");
	}
}

template <unsigned int T>
void breep::tcp::network_manager<T>::process_disconnection(peernm& disconnected_peer) {
	const std::vector<peernm>& others = m_owner->self().bridging_from_to().at(disconnected_peer.id());
	if (!others.empty()) {
		std::vector<uint8_t> vect(breep::detail::to_bigendian1<std::string, std::vector<uint8_t>>(boost::uuids::to_string(disconnected_peer.id())));
		vect.insert(vect.cbegin(), static_cast<uint8_t>(commands::peer_disconnection)); // boost::asio::buffer cannot take deque as parameter :(
		auto buffer = boost::asio::buffer(vect);
		for (const peernm& p : others) {
			boost::asio::async_write(
					*p.m_socket,
					buffer,
			        boost::bind(&network_manager<T>::write_done, this, _1, _2)
			);
		}
	}
}


template <unsigned int T>
inline void breep::tcp::network_manager<T>::write_done(boost::system::error_code, std::size_t) {
	// ignored for now.
}