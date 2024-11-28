# PulseVibe
**C Implementation of Heartbeats in Distributed Systems**

**Author**: Pruthvi Prakash Navada

## Instructions to Run the Program
1. Ensure that all the required files are in the same directory (`Dockerfile`, `docker-compose.yml`, `peer.c`, `hostsfile.txt`).
2. Navigate to the directory containing these files.
3. Build the Docker image using the following command:
   ```bash
   docker build -t prj1 .
   ```
4. Set up the cluster and start the containers using the following command. Make sure that port 8080 is available:
   ```bash
   docker-compose up
   ```

## Limitations
Occasionally, some processes may hang due to missed heartbeats. Adding an initial delay was attempted but did not resolve the issue. A possible workaround is to continuously send heartbeats until an acknowledgment is received.