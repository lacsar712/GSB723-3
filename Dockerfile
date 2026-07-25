FROM alpine:latest

# Install build dependencies
RUN apk add --no-cache gcc musl-dev libc-dev

WORKDIR /app

# Copy all source files
COPY . .

# Compile the C application statically for maximum compatibility in Alpine
RUN gcc -O2 backend/main.c backend/routes.c backend/database.c backend/json_parser.c backend/logger.c backend/credit.c -o server -static -lm

# Expose the application port
EXPOSE 2082

# Start the server
CMD ["./server"]
