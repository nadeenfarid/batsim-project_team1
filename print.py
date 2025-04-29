from evalys.jobset import JobSet
import matplotlib
matplotlib.use('Agg')  # Utilise un backend qui ne nécessite pas d'affichage
import matplotlib.pyplot as plt
js = JobSet.from_csv("out/jobs.csv")
js.plot(with_details=True)
plt.savefig("out/easy.png")
