FROM node:24-alpine
WORKDIR /app
COPY server.mjs ./server.mjs
COPY public ./public
RUN mkdir -p /data/games && chown -R node:node /data
ENV PORT=3000 WORKSHOP_GAME_DIR=/data/games
USER node
EXPOSE 3000
CMD ["node", "server.mjs"]
