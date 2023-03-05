import os
Import("env")

protobuf_folder = os.path.join(os.getcwd())
protobuf_transaction_file = f"{protobuf_folder}/transaction.proto"
protobuf_transaction_options_file = f"{protobuf_folder}/transaction.options"
project_dir = env.subst("$PROJECT_DIR")

try:
    os.symlink(protobuf_transaction_file, f"{project_dir}/protobuf/transaction.proto")
    print("Symlink created for transactions.proto file")
except FileExistsError:
    print("Symlink already exits for transactions.proto file")

try:
    os.symlink(protobuf_transaction_options_file, f"{project_dir}/protobuf/transaction.options")
    print("Symlink created for transactions.options file")
except FileExistsError:
    print("Symlink already exits transactions.options file")