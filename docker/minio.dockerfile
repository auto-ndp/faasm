FROM minio/minio:RELEASE.2022-10-08T20-11-00Z

CMD ["server", "/data/minio"]
