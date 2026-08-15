# Deploying the corpus MCP server to AWS Lambda

Infrastructure-as-code for serving the baked-in corpus MCP image
(`tools/mcp/Containerfile.lambda`) as a public HTTPS endpoint. This codifies
Part 4 of [design/archive/mcp-baked-in-lambda.md](../../../design/archive/mcp-baked-in-lambda.md).

```
ECR (image)  →  Lambda (container, arm64)  →  API Gateway HTTP API  →  corpus.koskari.org
```

- `template.yaml` — AWS SAM stack: the Lambda function, the HTTP API with
  stage throttling, and the custom domain + mapping. Parameterized by
  `ServerName`, `ImageUri`, `DomainName`, `CertificateArn` so the same
  template instantiates future MCP servers.
- `../../.github/workflows/deploy_mcp_lambda.yml` — on corpus/server change,
  rebuilds the arm64 image and runs `aws lambda update-function-code`,
  authenticating via GitHub OIDC.

## One-time bring-up

These are the manual, credentialed steps; they are yours to run once. The
workflow takes over redeploys afterward.

1. **ECR repository** for the image:
   ```bash
   aws ecr create-repository --repository-name koskari-mcp
   ```
2. **ACM certificate** for the domain, in the API's region (a regional HTTP
   API custom domain needs a same-region cert). Request it, then add the
   ACM-provided CNAME to koskari.org DNS to validate. Note the certificate
   ARN. ACM public certs are free and auto-renew while that CNAME stays in
   place.
3. **First image push** by hand, to prove the pipeline before wiring CI:
   ```bash
   make build-mcp-lambda MCP_ARCH=arm64
   aws ecr get-login-password --region "$REGION" \
     | docker login --username AWS --password-stdin "$ACCOUNT.dkr.ecr.$REGION.amazonaws.com"
   docker tag koskari-mcp:lambda "$ACCOUNT.dkr.ecr.$REGION.amazonaws.com/koskari-mcp:bootstrap"
   docker push "$ACCOUNT.dkr.ecr.$REGION.amazonaws.com/koskari-mcp:bootstrap"
   ```
4. **Deploy the stack** with that image and the cert ARN:
   ```bash
   sam deploy \
     --template-file tools/mcp/deploy/template.yaml \
     --stack-name koskari-mcp \
     --capabilities CAPABILITY_IAM \
     --image-repositories McpFunction="$ACCOUNT.dkr.ecr.$REGION.amazonaws.com/koskari-mcp" \
     --parameter-overrides \
       ImageUri="$ACCOUNT.dkr.ecr.$REGION.amazonaws.com/koskari-mcp:bootstrap" \
       DomainName=corpus.koskari.org \
       CertificateArn="$CERT_ARN"
   ```
   Run `sam validate --template-file tools/mcp/deploy/template.yaml` first.
5. **DNS.** Read the `CustomDomainTarget` stack output and add a CNAME
   `corpus.koskari.org` → that regional target at your DNS provider (an alias
   A record instead if the zone lives in Route 53).
6. **Verify** against the `McpEndpoint` output, then register it in an MCP
   client: `{"type": "http", "url": "https://corpus.koskari.org/mcp"}`.

## Wiring CI

The deploy workflow assumes an IAM role via GitHub OIDC -- no stored AWS keys.

1. Create a deploy role trusting `token.actions.githubusercontent.com` for
   this repo's subject, with permissions to push to the ECR repo and call
   `lambda:UpdateFunctionCode` on the function.
2. Add the role ARN as the `AWS_MCP_DEPLOY_ROLE` secret (environment
   `corpus-mcp`).
3. Adjust `AWS_REGION` / `ECR_REPOSITORY` / `FUNCTION_NAME` in the workflow
   `env:` block if they differ from the defaults.

After that, a push touching `docs/corpus/records/**` or `tools/mcp/**`
rebuilds and redeploys; `workflow_dispatch` triggers a manual redeploy.

## Cold start, throttling, cost

- Container-image cold start (image init + koskari bootstrap + parquet load)
  is a couple of seconds. If that is unacceptable, set provisioned
  concurrency = 1 (SnapStart is not available for container images).
- The stage throttle (`ThrottleRateLimit` / `ThrottleBurstLimit`) and
  `ReservedConcurrency` cap cost and abuse globally. Per-IP limiting needs
  WAF, which HTTP APIs do not support directly -- front with CloudFront+WAF
  or switch to a REST API only if traffic justifies it.
- At informational traffic this sits near the free tier: pay-per-request,
  scales to zero. Standing costs are ECR storage (cents) and provisioned
  concurrency if enabled.
