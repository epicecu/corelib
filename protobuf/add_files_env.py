import os
Import("env")

protobuf_folder = os.path.join(os.getcwd(), '.')

# env["custom_nanopb_protos"] = env["custom_nanopb_protos"] +  f" +<{protobuf_folder}/*.proto>"
# env["custom_nanopb_options"] = env["custom_nanopb_options"] + f" --error-on-unmatched"

global_env = DefaultEnvironment()

global_env["custom_nanopb_protos"] = global_env["custom_nanopb_protos"] +  f" +<{protobuf_folder}/*.proto>"
global_env["custom_nanopb_options"] = global_env["custom_nanopb_options"] + f" --error-on-unmatched"