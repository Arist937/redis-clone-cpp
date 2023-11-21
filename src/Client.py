import socket
import time

def send_command(sock, command):
    """ Send a command to the server and print the response. """
    sock.sendall(command.encode())
    response = sock.recv(1024)
    print(f"Response: {response.decode()}")

def main():
    # Server's IP address and port
    server_ip = '0.0.0.0'
    server_port = 6379

    # Create a socket object
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        # Connect to the server
        sock.connect((server_ip, server_port))
        print(f"Connected to server at {server_ip}:{server_port}")

        # Send some commands
        send_command(sock, "*1\r\n$4\r\nPING\r\n")

        # Wait a bit before closing the connection
        time.sleep(2)

if __name__ == "__main__":
    main()