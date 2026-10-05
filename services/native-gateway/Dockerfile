FROM rust:1.95-bookworm AS build
WORKDIR /src
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo build --locked --release

FROM debian:bookworm-slim
RUN useradd --create-home --uid 10001 halo
COPY --from=build /src/target/release/halo-native-gateway /usr/local/bin/halo-native-gateway
USER 10001:10001
EXPOSE 8080/tcp
EXPOSE 40000-40127/udp
ENTRYPOINT ["/usr/local/bin/halo-native-gateway"]
