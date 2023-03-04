import os
Import("env")

protobuf_folder = os.path.join(os.getcwd(), '.')

env["custom_nanopb_protos"] = f" +<{protobuf_folder}/*.proto>"
env["custom_nanopb_options"] = f" --error-on-unmatched"

# global_env = DefaultEnvironment()

# global_env.Append(
#     custom_nanopb_protos=f"+<{protobuf_folder}/*.proto>"
# )

# global_env.Append(
#     custom_nanopb_options=f"--error-on-unmatched"
# )
