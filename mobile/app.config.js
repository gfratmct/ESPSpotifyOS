module.exports = ({ config }) => {
  const associatedDomain = process.env.SPOTIFY_ASSOCIATED_DOMAIN?.trim();
  return {
    ...config,
    ios: {
      ...config.ios,
      ...(associatedDomain
        ? { associatedDomains: [`applinks:${associatedDomain}`] }
        : {}),
    },
  };
};
