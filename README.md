# This workflow installs the latest version of Terraform CLI and configures the Terraform CLI configuration file
# with an API token for Terraform Cloud (app.terraform.io). On pull request events, this workflow will run
# `terraform init`, `terraform fmt`, and `terraform plan` (speculative plan via Terraform Cloud). On push events
# to the "master" branch, `terraform apply` will be executed.
#
# Documentation for `hashicorp/setup-terraform` is located here: https://github.com/hashicorp/setup-terraform
#
# To use this workflow, you will need to complete the following setup steps.
#
# 1. Create a `main.tf` file in the root of this repository with the `remote` backend and one or more resources defined.
# 2. Generate a Terraform Cloud user API token and store it as a GitHub secret (e.g. TF_API_TOKEN) on this repository.
# 3. Reference the GitHub secret in the Setup Terraform step using the `hashicorp/setup-terraform` GitHub Action.
name: 'Terraform'

on:
  push:
    branches: [ "master" ]
  pull_request:

permissions:
  contents: read

jobs:
  terraform:
    name: 'Terraform'
    runs-on: ubuntu-latest
    environment: production

    # Use the Bash shell regardless whether the GitHub Actions runner is ubuntu-latest, macos-latest, or windows-latest
    defaults:
      run:
        shell: bash

    steps:
    - name: Checkout
      uses: actions/checkout@v4

    # Skip running Terraform unless this repo actually contains Terraform files.
    - name: Detect Terraform files
      id: detect_tf
      run: |
        # Check for any tracked .tf files in the repository
        if git ls-files '*.tf' | grep -q .; then
          echo "found=true" >> $GITHUB_OUTPUT
        else
          echo "found=false" >> $GITHUB_OUTPUT
        fi

    # If Terraform files are present, ensure a TF API token is configured. This avoids terraform init failing when
    # a remote backend (Terraform Cloud) requires authentication.
    - name: Require TF API Token
      if: steps.detect_tf.outputs.found == 'true'
      id: token_check
      run: |
        if [ -z "${{ secrets.TF_API_TOKEN }}" ]; then
          echo "has_token=false" >> $GITHUB_OUTPUT
        else
          echo "has_token=true" >> $GITHUB_OUTPUT
        fi

    # Install the latest version of Terraform CLI and configure the Terraform CLI configuration file with a Terraform Cloud user API token
    - name: Setup Terraform
      if: steps.detect_tf.outputs.found == 'true' && steps.token_check.outputs.has_token == 'true'
      uses: hashicorp/setup-terraform@v1
      with:
        cli_config_credentials_token: ${{ secrets.TF_API_TOKEN }}

    - name: Terraform Init
      if: steps.detect_tf.outputs.found == 'true' && steps.token_check.outputs.has_token == 'true'
      run: terraform init

    - name: Terraform Format
      if: steps.detect_tf.outputs.found == 'true' && steps.token_check.outputs.has_token == 'true'
      run: terraform fmt -check

    - name: Terraform Plan
      if: steps.detect_tf.outputs.found == 'true' && steps.token_check.outputs.has_token == 'true'
      run: terraform plan -input=false

    # On push to "master", build or change infrastructure according to Terraform configuration files
    - name: Terraform Apply
      if: steps.detect_tf.outputs.found == 'true' && steps.token_check.outputs.has_token == 'true' && github.ref == 'refs/heads/master' && github.event_name == 'push'
      run: terraform apply -auto-approve -input=false
