#!/bin/bash
# Exit on error
set -e

echo "=================================================="
echo "      Docker Hub Image Tag & Push Helper"
echo "=================================================="
echo ""

# Check if logged in to Docker
if ! docker system info | grep -q "Username"; then
    echo "It looks like you are not logged in to Docker Hub."
    echo "Please run 'docker login' first, or enter credentials when prompted next."
    echo ""
    docker login
fi

# Ask for username
read -p "Enter your Docker Hub username: " DOCKER_USER
if [ -z "$DOCKER_USER" ]; then
    echo "Error: Username cannot be empty."
    exit 1
fi

IMAGE_TAG="${DOCKER_USER}/research-explorer:latest"

echo ""
echo "1. Tagging local image 'dsa_project-backend:latest' as '${IMAGE_TAG}'..."
docker tag dsa_project-backend:latest "${IMAGE_TAG}"

echo ""
echo "2. Pushing '${IMAGE_TAG}' to Docker Hub..."
docker push "${IMAGE_TAG}"

echo ""
echo "=================================================="
echo "Successfully pushed image to Docker Hub!"
echo "Image URL: https://hub.docker.com/r/${DOCKER_USER}/research-explorer"
echo "=================================================="
