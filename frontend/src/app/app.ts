import { Component, ViewChild, ElementRef, signal } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { FormsModule } from '@angular/forms';
import { firstValueFrom } from 'rxjs';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './app.html',
  styleUrl: './app.scss'
})
export class App {
  title = 'Consola MIA';
  input: string = '';
  output = signal('Bienvenido a C++ DISK Web. Servidor listo...\n> ');
  currentScreen: 'console' | 'login' | 'explorer' = 'console'; 
  isLoggedIn: boolean = false; 
  isLoading: boolean = false;  

  // --- DATOS Y ESTADOS ---
  loginData = { user: '', pass: '', id: '' };
  currentPath: string = '/';
  fileSystemItems = [
    { name: 'home', type: 'folder' },
    { name: 'user.txt', type: 'file' },
    { name: 'proyecto.cpp', type: 'file' }
  ];

  @ViewChild('scrollMe') private myScrollContainer!: ElementRef;
  @ViewChild('fileInput') private fileInput!: ElementRef<HTMLInputElement>;

  constructor(private http: HttpClient) {}

  goToLogin() { this.currentScreen = 'login'; }
  goToConsole() { this.currentScreen = 'console'; }
  goToExplorer() { this.currentScreen = 'explorer'; }

  async onLogin() {
    const { user, pass, id } = this.loginData;
    if (!user || !pass || !id) return alert('Llene todos los campos');

    this.isLoading = true; 
    const comando = `login -user=${user} -pass=${pass} -id=${id}`;
    this.output.update(t => t + comando + '\n');

    try {
      const response: any = await firstValueFrom(
        this.http.post('http://localhost:8080/api/execute', { comando: comando })
      );
      
      const salidaCpp: string = response.salida || '';
      this.output.update(texto => texto + salidaCpp + '\n> ');

      const hayError = salidaCpp.toLowerCase().includes('error') || 
                       salidaCpp.toLowerCase().includes('incorrecto') ||
                       salidaCpp.toLowerCase().includes('fallo');

      if (!hayError && salidaCpp.trim() !== '') {
        this.isLoggedIn = true;        
        this.currentScreen = 'console';  
        this.loginData = { user: '', pass: '', id: '' }; 
      } else {
        alert("Error reportado por C++:\n" + salidaCpp);
      }

    } catch (err) {
      console.error(err);
      alert('Error de conexión. Verifica que el backend C++ esté corriendo.');
    } finally {
      this.isLoading = false; 
      this.scrollToBottom();
    }
  }

  async logout() {
    this.isLoggedIn = false;
    this.currentScreen = 'console'; 
    this.loginData = { user: '', pass: '', id: '' }; 
    await this.processSingleCommand('logout'); // Le avisamos al C++
  }

  openItem(item: any) {
    if (item.type === 'folder') {
      this.currentPath += `${item.name}/`;
    } else {
      this.currentScreen = 'console';
      this.processSingleCommand(`cat -file1=${this.currentPath}${item.name}`);
    }
  }

  async executeCommand() {
    if (!this.input.trim()) return;
    const cmd = this.input;
    this.input = ''; 
    await this.processSingleCommand(cmd);
  }

  async processSingleCommand(comando: string) {
    this.output.update(texto => texto + comando + '\n');
    this.scrollToBottom();

    try {
      const response: any = await firstValueFrom(
        this.http.post('http://localhost:8080/api/execute', comando, { responseType: 'text' })
      );
      this.output.update(texto => texto + response + '\n> ');
    } catch (err) {
      console.error(err);
      this.output.update(texto => texto + 'Error de conexión con el servidor C++\n> ');
    }
    
    this.scrollToBottom();
  }

  triggerFileInput() {
    this.fileInput.nativeElement.click();
  }

  async onFileSelected(event: any) {
    const file: File = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = async (e) => {
      const text = e.target?.result as string;
      if (!text) return;

      const lineas = text.split('\n');
      this.output.update(texto => texto + `[SCRIPT] Iniciando ejecución de: ${file.name}...\n> `);

      for (let linea of lineas) {
        linea = linea.trim();
        const comandoReal = linea.split('#')[0].trim();
        if (!comandoReal) continue;
        await this.processSingleCommand(comandoReal);
      }
      
      this.output.update(texto => texto + `[SCRIPT] Ejecución finalizada.\n> `);
      this.scrollToBottom();
      this.fileInput.nativeElement.value = '';
    };
    reader.readAsText(file);
  }

  scrollToBottom() {
    setTimeout(() => {
      try {
        this.myScrollContainer.nativeElement.scrollTop = this.myScrollContainer.nativeElement.scrollHeight;
      } catch (err) {}
    }, 50);
  }
}