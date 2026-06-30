# WsprryPi Support GPT Test Prompts

Use these prompts after updating the WsprryPi Support GPT instructions, uploaded Knowledge files, or support-bundle workflow.

The goal is to verify that the GPT:

- introduces the support bundle as a normal diagnostic step
- explains that most users may not already know what it is
- walks the user through creating it with the official script
- offers a review-first alternative to `curl | bash`
- does not invent documentation that does not exist
- diagnoses from uploaded evidence before guessing
- preserves safe RF, privacy, and timing guidance

## Support bundle introduction

### Prompt

```text
My WsprryPi install is not working. What should I do?
