#include "WiredConnection.h"
#include "ServerError.hpp"

#include <libimobiledevice/common/socket.h>

// Must come after other #include's
#include "AltInclude.h"

// Copied from imobiledevice/common/socket.c
static int verbose = 0;
int socket_check_fd(int fd, fd_mode fdm, unsigned int timeout)
{
	fd_set fds;
	int sret;
	int eagain;
	struct timeval to;
	struct timeval* pto;

	if (fd < 0) {
		if (verbose >= 2)
			fprintf(stderr, "ERROR: invalid fd in check_fd %d\n", fd);
		return -1;
	}

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	sret = -1;

	do {
		if (timeout > 0) {
			to.tv_sec = (time_t)(timeout / 1000);
			to.tv_usec = (time_t)((timeout - (to.tv_sec * 1000)) * 1000);
			pto = &to;
		}
		else {
			pto = NULL;
		}
		eagain = 0;
		switch (fdm) {
		case FDM_READ:
			sret = select(fd + 1, &fds, NULL, NULL, pto);
			break;
		case FDM_WRITE:
			sret = select(fd + 1, NULL, &fds, NULL, pto);
			break;
		case FDM_EXCEPT:
			sret = select(fd + 1, NULL, NULL, &fds, pto);
			break;
		default:
			return -1;
		}

		if (sret < 0) {
			switch (errno) {
			case EINTR:
				// interrupt signal in select
				if (verbose >= 2)
					fprintf(stderr, "%s: EINTR\n", __func__);
				eagain = 1;
				break;
			case EAGAIN:
				if (verbose >= 2)
					fprintf(stderr, "%s: EAGAIN\n", __func__);
				break;
			default:
				if (verbose >= 2)
					fprintf(stderr, "%s: select failed: %s\n", __func__,
						strerror(errno));
				return -1;
			}
		}
		else if (sret == 0) {
			return -ETIMEDOUT;
		}
	} while (eagain);

	return sret;
}

WiredConnection::WiredConnection(std::shared_ptr<Device> device, idevice_connection_t connection) : _device(device), _connection(connection)
{
}

WiredConnection::~WiredConnection()
{
}

void WiredConnection::Disconnect()
{
	if (this->connection() == NULL)
	{
		return;
	}

	idevice_disconnect(this->connection());
	_connection = NULL;
}

pplx::task<void> WiredConnection::SendData(std::vector<unsigned char>& data)
{
	return pplx::create_task([data, this]() mutable {
		int fd = 0;
		idevice_error_t result = idevice_connection_get_fd(this->connection(), &fd);
		if (result != IDEVICE_E_SUCCESS || fd <= 0)
		{
			altlog("Error getting WiredConnection file descriptor. " << result);
			throw ServerError(ServerErrorCode::LostConnection);
		}

		int socket_result = socket_check_fd(fd, FDM_READ, 1);
		if (socket_result != 0 && socket_result != -ETIMEDOUT)
		{
			altlog("Error checking WiredConnection file descriptor " << fd << ". " << socket_result);
			throw ServerError(ServerErrorCode::LostConnection);
		}

		while (data.size() > 0)
		{
			uint32_t sentBytes = 0;
			idevice_error_t result = idevice_connection_send(this->connection(), (const char*)data.data(), (int32_t)data.size(), &sentBytes);
			if (result != IDEVICE_E_SUCCESS || sentBytes == 0)
			{
				altlog("Error sending data over WiredConnection. " << result);
				throw ServerError(ServerErrorCode::LostConnection);
			}

			data.erase(data.begin(), data.begin() + sentBytes);
		}
	});
}

pplx::task<std::vector<unsigned char>> WiredConnection::ReceiveData(int expectedSize)
{
	return pplx::create_task([=]() -> std::vector<unsigned char> {
		char bytes[4096];

		std::vector<unsigned char> receivedData;
		receivedData.reserve(expectedSize);

		while (receivedData.size() < expectedSize)
		{
			uint32_t size = min((uint32_t)4096, (uint32_t)expectedSize - (uint32_t)receivedData.size());

			uint32_t receivedBytes = 0;
			idevice_error_t result = idevice_connection_receive_timeout(this->connection(), bytes, size, &receivedBytes, 10000);
			if (result != IDEVICE_E_SUCCESS || receivedBytes == 0)
			{
				altlog("Error receiving data over WiredConnection. " << result);
				throw ServerError(ServerErrorCode::LostConnection);
			}

			receivedData.insert(receivedData.end(), bytes, bytes + receivedBytes);
		}

		return receivedData;
	});
}

std::shared_ptr<Device> WiredConnection::device() const
{
	return _device;
}

idevice_connection_t WiredConnection::connection() const
{
	return _connection;
}
