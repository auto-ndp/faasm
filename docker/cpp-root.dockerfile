FROM kubasz51/faasm-faabric-base:0.1.5

# -------------------------------------------------------------
# NOTE - extensive use of Ansible in this Dockerfile makes it
# poor at taking advantage of the Docker build cache, so it can
# be a pain to develop. This is a necessary evil that avoids the
# need to create and maintain install/ setup code in this Dockerfile
# that already exists in the Ansible playbooks.
# -------------------------------------------------------------

# We could be more tactical here, adding only what's required, thus
# avoiding invalidating the Docker cache when anything Ansible-related
# changes. However, this image is not rebuilt often, so it's probably
# unnecessary.

COPY ansible /usr/local/code/faasm/ansible
WORKDIR /usr/local/code/faasm/ansible

RUN ansible-playbook llvm.yml

RUN apt-get -y clean autoclean
RUN apt-get -y autoremove

CMD /bin/bash
