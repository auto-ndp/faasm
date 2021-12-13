FROM minio/minio:RELEASE.2021-12-10T23-03-39Z

CMD ["server", "/data/minio"]
