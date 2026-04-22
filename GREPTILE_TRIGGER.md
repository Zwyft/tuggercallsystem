# Greptile Trigger Instructions

## How to Trigger Greptile Analysis

### Method 1: GitHub Actions (Recommended)
1. **Configure API Token**:
   - Go to your repository Settings → Secrets and variables → Actions
   - Add a new repository secret named `GREPTILE_API_TOKEN`
   - Paste your Greptile API token

2. **Push Changes**:
   ```bash
   git add .github/workflows/greptile-analysis.yml
   git commit -m "ci: Add Greptile GitHub Actions workflow"
   git push origin dev-optimized-eink
   ```

3. **Check Results**:
   - Go to the "Actions" tab in your GitHub repository
   - Look for "Greptile Code Analysis" workflow

### Method 2: Manual Trigger via Comment
Add a comment to any commit or pull request with:

```
@greptile analyze this commit
```

### Method 3: Commit Message Keywords
Include these keywords in your commit message to trigger Greptile:

```
greptile analyze
greptile scan
greptile review
```

## Expected Behavior

Once Greptile is configured, it should:

1. **Automatically scan** commits to the specified branches
2. **Generate reports** with:
   - Bug detection and categorization
   - Security vulnerability assessment
   - Performance optimization suggestions
   - Code quality metrics

3. **Create detailed comments** on commits with:
   - Specific line numbers where issues are found
   - Severity levels (Critical, High, Medium, Low)
   - Fix recommendations
   - Code examples

## Troubleshooting

If Greptile is still not working:

1. **Check API Token**: Ensure `GREPTILE_API_TOKEN` is correctly configured
2. **Verify Branch**: Make sure you're pushing to the right branch (main, dev, dev-optimized-eink)
3. **Check Logs**: Look at the GitHub Actions logs for errors
4. **Wait Time**: Greptile may take a few minutes to process new commits

## Current Test Status

- ✅ GitHub Actions workflow configured
- ✅ Test commits created with bug analysis
- ⚠️  Awaiting API token configuration
- 📋 Analysis framework ready for testing

## Files Created for Testing

- `.github/workflows/greptile-analysis.yml`: GitHub Actions workflow
- `.greptile.json`: Greptile configuration file
- `GREPTILE_TRIGGER.md`: This trigger guide
- `firmware/bug-analysis-greptile.py`: Local analysis script
- `firmware/greptile-bug-analysis.md`: Bug analysis report
