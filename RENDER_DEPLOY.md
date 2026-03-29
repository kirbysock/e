# Render Deploy

Use the root `render.yaml` Blueprint.

1. Push this repository to GitHub.
2. In Render, create a new Blueprint from the repo.
3. Render will create:
   - `treex-chatting-api-20260329` as the web service
   - `treex-chatting-db-20260329` as the Postgres database
4. After the web service is live, confirm its public URL.
5. If Render gives a different URL than the one in `src/serverconfig.h`, update `kDefaultCentralApiBaseUrl` and rebuild the Qt app.

Notes:

- The backend code lives in `backend/`.
- The Qt desktop client talks to the hosted HTTP API.
- For local client testing, set `CHAT_APP_API_BASE_URL` to your local API URL before launching the app.
