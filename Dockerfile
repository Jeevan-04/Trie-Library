# Build stage
FROM alpine:3.18 AS builder

# Install build dependencies: clang, make, and musl-dev (for standard headers)
RUN apk add --no-cache clang make musl-dev

WORKDIR /app

# Copy all source code (Makefiles, C source and header files, HTML/CSS/JS files)
COPY . .

# Clean any existing local builds and compile the binary inside Alpine
RUN make clean && make CC=clang

# Run stage
FROM alpine:3.18

WORKDIR /app

# Copy the compiled binary from the builder stage
COPY --from=builder /app/research_explorer .

# Copy HTML/CSS/JS frontend files which are served as static files by the server
COPY index.html .
COPY index.css .
COPY index.js .

# Copy the gzipped subset database and decompress it
COPY arxiv-metadata-oai-snapshot.json.gz .
RUN gzip -d arxiv-metadata-oai-snapshot.json.gz

# Expose HTTP port 10000
EXPOSE 10000

# Run the backend server
CMD ["./research_explorer"]
