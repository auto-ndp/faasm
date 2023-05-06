FROM alannair/faasm-ceph-base:latest
ARG FAASM_VERSION

WORKDIR .

# copy our changes 
COPY ./ceph/src ./ceph/src
COPY ./bin/buildceph.sh .
RUN chmod +x ./buildceph.sh && ./buildceph.sh

# WORKDIR /ceph
# RUN rm -rf build \
#     && ./do_cmake.sh \
#     && cd build \
#     && ninja \
#     && ninja install

# RUN cd /ceph/build && ninja && ninja install 

# after building save build folder to host to speed up the next docker build
# docker cp -r <CONTAINER_ID>:/usr/local/code/faasm/ceph/build ./ceph/build