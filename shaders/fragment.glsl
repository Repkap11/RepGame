const vec3 AMBIENT = vec3(0.2, 0.2, 0.2);
const float MAX_DIST = 1;
const float MAX_DIST_SQUARED = MAX_DIST * MAX_DIST;

uniform vec3 lightColor;

varying vec3 fragmentNormal;
varying vec3 cameraVector;
varying vec3 lightVector;
varying float cameraDistance;

void
main()
{
	// normalize the fragment normal and camera direction
	vec3 normal = normalize(fragmentNormal);
	vec3 cameraDir = normalize(cameraVector);

	// calculate distance between 0.0 and 1.0
	float dist = min(dot(lightVector, lightVector), MAX_DIST_SQUARED) / MAX_DIST_SQUARED;
	float distFactor = 1.0 - dist;

	// diffuse
	vec3 lightDir = normalize(lightVector);
	float diffuseDot = dot(normal, lightDir);
	vec3 diffuse = lightColor * clamp(diffuseDot, 0.0, 1.0) * distFactor;

	// specular
	vec3 halfAngle = normalize(cameraDir + lightDir);
	vec3 specularColor = min(lightColor + 0.5, 1.0);
	float specularDot = dot(normal, halfAngle);
	vec3 specular = specularColor * pow(clamp(specularDot, 0.0, 1.0), 16.0) * distFactor;

	//gl_FragColor = vec4(clamp((diffuse + AMBIENT) + specular, 0.0, 1.0), 1);
	gl_FragColor = vec4(normal + normalize(vec3(1,1,1)), 1);
	float color = cameraDistance / 50;
	gl_FragColor = vec4(cameraDir,1);

	//gl_FragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
}
