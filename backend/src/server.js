const crypto = require("crypto");
const express = require("express");
const bcrypt = require("bcryptjs");
const { Pool } = require("pg");

const port = Number.parseInt(process.env.PORT || "10000", 10);
const databaseUrl = process.env.DATABASE_URL;

if (!databaseUrl) {
  throw new Error("DATABASE_URL is required.");
}

const pool = new Pool({
  connectionString: databaseUrl,
  ssl: process.env.RENDER === "true" ? { rejectUnauthorized: false } : false,
});

const app = express();
app.use(express.json({ limit: "1mb" }));

function fail(response, statusCode, message) {
  response.status(statusCode).json({ error: message });
}

async function ensureSchema() {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS users (
      id BIGSERIAL PRIMARY KEY,
      nickname TEXT NOT NULL,
      email TEXT NOT NULL,
      password_hash TEXT NOT NULL,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE UNIQUE INDEX IF NOT EXISTS users_nickname_lower_idx
      ON users (LOWER(nickname));
    CREATE UNIQUE INDEX IF NOT EXISTS users_email_lower_idx
      ON users (LOWER(email));

    CREATE TABLE IF NOT EXISTS sessions (
      token TEXT PRIMARY KEY,
      user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE INDEX IF NOT EXISTS sessions_user_id_idx ON sessions(user_id);

    CREATE TABLE IF NOT EXISTS friendships (
      user_one_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      user_two_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      PRIMARY KEY (user_one_id, user_two_id)
    );

    CREATE INDEX IF NOT EXISTS friendships_user_one_idx ON friendships(user_one_id);
    CREATE INDEX IF NOT EXISTS friendships_user_two_idx ON friendships(user_two_id);

    CREATE TABLE IF NOT EXISTS friend_requests (
      id BIGSERIAL PRIMARY KEY,
      sender_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      receiver_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      status TEXT NOT NULL DEFAULT 'pending',
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      responded_at TIMESTAMPTZ
    );

    CREATE INDEX IF NOT EXISTS friend_requests_receiver_status_idx
      ON friend_requests(receiver_id, status, created_at);
    CREATE INDEX IF NOT EXISTS friend_requests_pair_status_idx
      ON friend_requests(sender_id, receiver_id, status);

    CREATE TABLE IF NOT EXISTS messages (
      id BIGSERIAL PRIMARY KEY,
      sender_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      receiver_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      body TEXT NOT NULL,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE INDEX IF NOT EXISTS messages_pair_created_at_idx
      ON messages(sender_id, receiver_id, created_at);
  `);
}

function normalizeNickname(value) {
  return String(value || "").trim();
}

function normalizeEmail(value) {
  return String(value || "").trim();
}

function orderedUserIds(firstUserId, secondUserId) {
  return firstUserId < secondUserId
    ? [firstUserId, secondUserId]
    : [secondUserId, firstUserId];
}

async function authenticatedUser(request) {
  const header = String(request.headers.authorization || "");
  const token = header.startsWith("Bearer ") ? header.slice(7).trim() : "";
  if (!token) {
    return null;
  }

  const query = await pool.query(
    `
      SELECT u.id, u.nickname, u.email, s.token
      FROM sessions s
      JOIN users u ON u.id = s.user_id
      WHERE s.token = $1
      LIMIT 1;
    `,
    [token]
  );

  if (query.rowCount === 0) {
    return null;
  }

  await pool.query("UPDATE sessions SET last_seen_at = NOW() WHERE token = $1;", [token]);
  return query.rows[0];
}

async function requireAuth(request, response, next) {
  try {
    const user = await authenticatedUser(request);
    if (!user) {
      fail(response, 401, "Your session is no longer valid. Please log in again.");
      return;
    }

    request.user = user;
    next();
  } catch (error) {
    next(error);
  }
}

async function usersAreFriends(firstUserId, secondUserId, client = pool) {
  const [userOneId, userTwoId] = orderedUserIds(firstUserId, secondUserId);
  const query = await client.query(
    `
      SELECT 1
      FROM friendships
      WHERE user_one_id = $1 AND user_two_id = $2
      LIMIT 1;
    `,
    [userOneId, userTwoId]
  );

  return query.rowCount > 0;
}

app.get("/api/status", async (_request, response) => {
  response.json({
    ok: true,
    service: "chatting-render-api",
    timestamp: new Date().toISOString(),
  });
});

app.post("/api/register", async (request, response, next) => {
  try {
    const nickname = normalizeNickname(request.body.nickname);
    const email = normalizeEmail(request.body.email);
    const password = String(request.body.password || "");

    if (!nickname) {
      fail(response, 400, "Nickname is required.");
      return;
    }

    if (!email || !email.includes("@")) {
      fail(response, 400, "Enter a valid email address.");
      return;
    }

    if (password.length < 6) {
      fail(response, 400, "Password must be at least 6 characters.");
      return;
    }

    const passwordHash = await bcrypt.hash(password, 10);

    try {
      await pool.query(
        `
          INSERT INTO users (nickname, email, password_hash)
          VALUES ($1, $2, $3);
        `,
        [nickname, email, passwordHash]
      );
    } catch (error) {
      if (error.code === "23505") {
        const detail = String(error.detail || "").toLowerCase();
        if (detail.includes("nickname")) {
          fail(response, 409, "That nickname is already taken.");
          return;
        }
        if (detail.includes("email")) {
          fail(response, 409, "That email is already registered.");
          return;
        }
        fail(response, 409, "That account already exists.");
        return;
      }

      throw error;
    }

    response.status(201).json({ ok: true });
  } catch (error) {
    next(error);
  }
});

app.post("/api/login", async (request, response, next) => {
  try {
    const nickname = normalizeNickname(request.body.nickname);
    const password = String(request.body.password || "");

    if (!nickname || !password) {
      fail(response, 400, "Nickname and password are required.");
      return;
    }

    const query = await pool.query(
      `
        SELECT id, nickname, email, password_hash
        FROM users
        WHERE LOWER(nickname) = LOWER($1)
        LIMIT 1;
      `,
      [nickname]
    );

    if (query.rowCount === 0) {
      fail(response, 404, "No account was found for that nickname.");
      return;
    }

    const user = query.rows[0];
    const passwordMatches = await bcrypt.compare(password, user.password_hash);
    if (!passwordMatches) {
      fail(response, 401, "Password is incorrect.");
      return;
    }

    const sessionToken = crypto.randomUUID();
    await pool.query(
      `
        INSERT INTO sessions (token, user_id)
        VALUES ($1, $2);
      `,
      [sessionToken, user.id]
    );

    response.json({
      session_token: sessionToken,
      user: {
        id: Number(user.id),
        nickname: user.nickname,
        email: user.email,
      },
    });
  } catch (error) {
    next(error);
  }
});

app.get("/api/friends", requireAuth, async (request, response, next) => {
  try {
    const query = await pool.query(
      `
        SELECT u.id, u.nickname, u.email
        FROM friendships f
        JOIN users u ON u.id = CASE
          WHEN f.user_one_id = $1 THEN f.user_two_id
          ELSE f.user_one_id
        END
        WHERE f.user_one_id = $1 OR f.user_two_id = $1
        ORDER BY LOWER(u.nickname) ASC;
      `,
      [request.user.id]
    );

    response.json({
      friends: query.rows.map((row) => ({
        id: Number(row.id),
        nickname: row.nickname,
        email: row.email,
      })),
    });
  } catch (error) {
    next(error);
  }
});

app.get("/api/friend-requests", requireAuth, async (request, response, next) => {
  try {
    const query = await pool.query(
      `
        SELECT fr.id, u.id AS sender_id, u.nickname AS sender_nickname,
               u.email AS sender_email, fr.created_at
        FROM friend_requests fr
        JOIN users u ON u.id = fr.sender_id
        WHERE fr.receiver_id = $1 AND fr.status = 'pending'
        ORDER BY fr.created_at ASC, fr.id ASC;
      `,
      [request.user.id]
    );

    response.json({
      requests: query.rows.map((row) => ({
        id: Number(row.id),
        sender_id: Number(row.sender_id),
        sender_nickname: row.sender_nickname,
        sender_email: row.sender_email,
        created_at: row.created_at,
      })),
    });
  } catch (error) {
    next(error);
  }
});

app.post("/api/friend-requests", requireAuth, async (request, response, next) => {
  try {
    const nickname = normalizeNickname(request.body.nickname);
    if (!nickname) {
      fail(response, 400, "Enter a nickname to send a request.");
      return;
    }

    const targetQuery = await pool.query(
      `
        SELECT id, nickname
        FROM users
        WHERE LOWER(nickname) = LOWER($1)
        LIMIT 1;
      `,
      [nickname]
    );

    if (targetQuery.rowCount === 0) {
      fail(response, 404, "No user was found with that nickname.");
      return;
    }

    const receiverId = Number(targetQuery.rows[0].id);
    const senderId = Number(request.user.id);

    if (receiverId === senderId) {
      fail(response, 400, "You cannot send a friend request to yourself.");
      return;
    }

    if (await usersAreFriends(senderId, receiverId)) {
      fail(response, 409, "That user is already in your friends list.");
      return;
    }

    const pendingQuery = await pool.query(
      `
        SELECT sender_id
        FROM friend_requests
        WHERE status = 'pending'
          AND ((sender_id = $1 AND receiver_id = $2)
            OR (sender_id = $2 AND receiver_id = $1))
        ORDER BY id DESC
        LIMIT 1;
      `,
      [senderId, receiverId]
    );

    if (pendingQuery.rowCount > 0) {
      const pendingSenderId = Number(pendingQuery.rows[0].sender_id);
      if (pendingSenderId === senderId) {
        fail(response, 409, "You already sent a pending request to that user.");
      } else {
        fail(response, 409, "That user already sent you a request. Accept it first.");
      }
      return;
    }

    await pool.query(
      `
        INSERT INTO friend_requests (sender_id, receiver_id)
        VALUES ($1, $2);
      `,
      [senderId, receiverId]
    );

    response.status(201).json({ ok: true });
  } catch (error) {
    next(error);
  }
});

app.post("/api/friend-requests/:requestId/accept", requireAuth, async (request, response, next) => {
  const client = await pool.connect();
  try {
    const requestId = Number.parseInt(request.params.requestId, 10);
    if (!Number.isFinite(requestId)) {
      fail(response, 400, "Invalid friend request id.");
      return;
    }

    await client.query("BEGIN");

    const friendRequestQuery = await client.query(
      `
        SELECT id, sender_id, receiver_id, status
        FROM friend_requests
        WHERE id = $1
        FOR UPDATE;
      `,
      [requestId]
    );

    if (friendRequestQuery.rowCount === 0) {
      await client.query("ROLLBACK");
      fail(response, 404, "That friend request no longer exists.");
      return;
    }

    const friendRequest = friendRequestQuery.rows[0];
    const receiverId = Number(friendRequest.receiver_id);
    const senderId = Number(friendRequest.sender_id);

    if (receiverId !== Number(request.user.id)) {
      await client.query("ROLLBACK");
      fail(response, 403, "You can only accept requests sent to your account.");
      return;
    }

    if (friendRequest.status !== "pending") {
      await client.query("ROLLBACK");
      fail(response, 409, "That request was already handled.");
      return;
    }

    const [userOneId, userTwoId] = orderedUserIds(senderId, receiverId);
    await client.query(
      `
        INSERT INTO friendships (user_one_id, user_two_id)
        VALUES ($1, $2)
        ON CONFLICT DO NOTHING;
      `,
      [userOneId, userTwoId]
    );

    await client.query(
      `
        UPDATE friend_requests
        SET status = 'accepted', responded_at = NOW()
        WHERE id = $1;
      `,
      [requestId]
    );

    await client.query("COMMIT");
    response.json({ ok: true });
  } catch (error) {
    await client.query("ROLLBACK");
    next(error);
  } finally {
    client.release();
  }
});

app.get("/api/conversations/:friendId", requireAuth, async (request, response, next) => {
  try {
    const friendId = Number.parseInt(request.params.friendId, 10);
    if (!Number.isFinite(friendId)) {
      fail(response, 400, "Invalid friend id.");
      return;
    }

    if (!(await usersAreFriends(Number(request.user.id), friendId))) {
      fail(response, 403, "You can only message users who accepted your friend request.");
      return;
    }

    const query = await pool.query(
      `
        SELECT id, sender_id, receiver_id, body, created_at
        FROM messages
        WHERE (sender_id = $1 AND receiver_id = $2)
           OR (sender_id = $2 AND receiver_id = $1)
        ORDER BY created_at ASC, id ASC;
      `,
      [request.user.id, friendId]
    );

    response.json({
      messages: query.rows.map((row) => ({
        id: Number(row.id),
        sender_id: Number(row.sender_id),
        receiver_id: Number(row.receiver_id),
        body: row.body,
        created_at: row.created_at,
      })),
    });
  } catch (error) {
    next(error);
  }
});

app.post("/api/messages", requireAuth, async (request, response, next) => {
  try {
    const receiverId = Number.parseInt(String(request.body.receiverId), 10);
    const body = String(request.body.body || "").trim();

    if (!Number.isFinite(receiverId) || receiverId <= 0) {
      fail(response, 400, "Pick a friend before sending.");
      return;
    }

    if (!body) {
      fail(response, 400, "Write a message before sending.");
      return;
    }

    if (!(await usersAreFriends(Number(request.user.id), receiverId))) {
      fail(response, 403, "You can only message users who accepted your friend request.");
      return;
    }

    await pool.query(
      `
        INSERT INTO messages (sender_id, receiver_id, body)
        VALUES ($1, $2, $3);
      `,
      [request.user.id, receiverId, body]
    );

    response.status(201).json({ ok: true });
  } catch (error) {
    next(error);
  }
});

app.use((error, _request, response, _next) => {
  console.error(error);
  fail(response, 500, "Internal server error.");
});

async function start() {
  await ensureSchema();
  app.listen(port, "0.0.0.0", () => {
    console.log(`Chat API listening on port ${port}`);
  });
}

start().catch((error) => {
  console.error(error);
  process.exit(1);
});
