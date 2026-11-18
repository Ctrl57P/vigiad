# Vigia

Proyecto final de Modelado y Simulación.

- **Alumno:** Cristian López García
- **Carrera:** Tecnologías para la Información en Ciencias
- **Materia:** Modelado y Simulación
- **Ciclo:** 2027-1

## ¿Por qué decidí hacer este proyecto?

Quería hacer un proyecto con bases que realmente entendiera y que fuera
posible terminar.

También quería monitorear el rendimiento de mi computadora y averiguar por
qué a veces funciona más lento o más rápido. Y si algún día se mete un
programa malicioso que esté usando recursos a escondidas, poder detectarlo
a tiempo.

## ¿Qué es Vigía?

Es un programa que corre en segundo plano en Linux (un daemon). Primero
aprende cómo se comporta la computadora cuando está normal, y después avisa
cuando algo se sale de lo normal y dice qué fue.

## ¿Qué mide?

- Temperatura del CPU
- Uso de la RAM
- Uso del CPU
- Presión del CPU, la memoria y el disco (PSI): cuánto tiempo se quedan
  esperando los programas por esos recursos
- Latencia: cuánto tarda el sistema en responder

Con estas mediciones saco un promedio de cómo está el sistema cuando todo
va bien. A eso le llamo la **baseline**.

## La matemática

**1. Calibración.** Con la computadora en reposo, saco el promedio $\mu$ y la
desviación estándar $\sigma$ de cada medición.

**2. Qué tan lejos está de lo normal.**

$$z = \frac{x - \mu}{\sigma}$$

Si $z = 0$, la medición está igual que siempre. Si $z = 3$, está 3
desviaciones estándar arriba de lo normal.

**3. Suavizado.** Las mediciones brincan mucho, así que mezclo la medición
nueva con las anteriores para que un pico aislado no cuente:

$$s_t = 0.3 \cdot z_t + 0.7 \cdot s_{t-1}$$

**4. Índice de degradación.** Me quedo con la medición que esté peor. Esa
es la causa probable.

$$D = \max(0,\ s_1,\ s_2,\ \dots,\ s_n)$$

El 0 está para que el índice nunca sea negativo.

**5. Alerta.** Si $D \geq 2$ durante 5 mediciones seguidas, avisa. Para
quitar la alerta, $D$ tiene que estar abajo de 2 otras 5 mediciones seguidas.

Un detalle: con la computadora en reposo, $\sigma$ sale casi cero y cualquier
cambio pequeño daría una $z$ enorme. Por eso le puse un valor mínimo a
$\sigma$ (por ejemplo 2 % para el CPU y 2 °C para la temperatura).

## Prueba

![Gráfica del experimento de demostración](docs/demo.png)

Dejé la computadora en reposo para calibrar y luego le metí dos cargas a
propósito:

- **CPU al 100 % por 45 segundos:** Vigía avisó 5 segundos después y dijo
  que la causa era el CPU.
- **Unos 8 GB de RAM ocupados por 45 segundos:** avisó 6 segundos después y
  dijo que la causa era la RAM.

No hubo alertas falsas. La alerta del CPU tardó más en quitarse porque el
procesador se quedó caliente un rato después de quitar la carga.

## Compilación y arranque

El daemon está hecho en C y se compila con optimizaciones para el
procesador de la computadora donde va a correr, para que sea lo más ligero
posible (usa como 2.5 MB de RAM).

```sh
make
make demo   # corre la prueba (como 5 minutos, sin usar la computadora)
```

Para la gráfica se necesita Python con `matplotlib`.

La idea es que Vigía arranque solo al prender la computadora como un
servicio de systemd:

```sh
sudo systemctl enable --now vigiad
```

## Lo que falta

- Hacer el servicio de systemd
- Cifrar los datos (por ahora se guardan sin cifrar)
- Probarlo varios días con uso normal, no solo unos minutos
- Que diga qué programa exactamente causó el problema
